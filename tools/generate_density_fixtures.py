#!/usr/bin/env python3
"""Current-ISA software probes; never encodes a proposed instruction."""
import hashlib
import json
from pathlib import Path
import sys
from isa import assemble, decode, parse
from generate_kernel_fixtures import image_symbols, IMAGES, control_registry
ROOT=Path(__file__).resolve().parents[1]
DIRECTORY=ROOT/'examples/workloads/density'

def sha(data):return hashlib.sha256(data).hexdigest()
def words(values):return list(b''.join((v&65535).to_bytes(2,'little') for v in values))
def cases():
    result=[]
    def add(name,source,checks=None,data=None,helpers=(),family='',comparison=None,origin=0x100,**extra):
        result.append(dict(name=name,source=source,checks=checks or [],data=data or [],helpers=list(helpers),family=family,comparison=comparison,origin=origin,**extra))
    def arithmetic(name,a,b,signed=False,simple=False,multiply=False):
        if multiply:q=(a*b)&65535;r=0;status=0
        elif b==0:q=r=0;status=1
        elif signed and a==-32768 and b==-1:q=r=0;status=2
        else:q=(abs(a)//abs(b))*(-1 if (a<0) != (b<0) else 1);r=a-q*b;status=0
        target='multiply' if multiply else 'signed_divide' if signed else 'divide'
        source=f'MOV.W #{a&65535},r0\nMOV.W #{b&65535},r1\nCALL {target}\nMOV.W r0,[$4000]\n'
        if not multiply:source+='MOV.W r1,[$4002]\n'
        source+='HALT\n'
        helper=['multiply.asm'] if multiply else (['signed_divide.asm'] if signed else [])+['divide_simple.asm' if simple else 'divide.asm']
        check=[(0x4000,words([q])) ,(0x3f00,words([status]))]
        if not multiply:check.append((0x4002,words([r])))
        add(name,source,check,helpers=helper,family='multiply' if multiply else 'divide_mod',inputs={'a':a,'b':b,'signed':signed},comparison='udiv_small_simple' if name=='udiv_small_binary' else None)
    for name,a,b in [('mul_unsigned',65535,32769),('mul_signed',-1234,27),('mul_zero',0,65535)]:arithmetic(name,a,b,multiply=True,signed=name=='mul_signed')
    for name,a,b in [('udiv_small_simple',100,7),('udiv_small_binary',100,7),('udiv_max',65535,1),('udiv_high',65535,32769),('udiv_less',17,65535),('udiv_zero',7,0)]:arithmetic(name,a,b,simple=name.endswith('simple'))
    for i,(a,b) in enumerate([(-100,7),(100,-7),(-100,-7),(-32768,1),(-32768,-1),(0,-7),(7,0)]):arithmetic(f'sdiv_{i}',a,b,signed=True)
    # Deterministic arithmetic coverage beyond the displayed examples.
    import random
    rng=random.Random(47017)
    for i in range(40):arithmetic(f'udiv_random_{i}',rng.randrange(65536),rng.randrange(65536))
    for i in range(24):arithmetic(f'sdiv_random_{i}',rng.randrange(-32768,32768),rng.randrange(-32768,32768),signed=True)
    for name,text,value,status in [('parse_max','65535',65535,0),('parse_zero','00000',0,0),('parse_overflow','65536',0,3),('parse_invalid','12x45',0,3)]:
        add(name,'CALL parse_decimal\nMOV.W r0,[$4200]\nHALT\n',[(0x4200,words([value])),(0x3f00,words([status]))],[(0x4000,list(text.encode()))],['decimal.asm','divide.asm'],'divide_mod')
    for n in (0,7,65535):
        add(f'format_{n}',f'MOV.W #{n},r0\nCALL format_decimal\nHALT\n',[(0x4100,list(f'{n:05d}'.encode()))],helpers=['decimal.asm','divide.asm'],family='divide_mod')
    for width in ('B','W'):
        stride=1 if width=='B' else 2;data=list(range(16*stride))
        for variant in ('unit','hoisted'):
            loop=f'MOV.{width} [r4],r0\nMOV.{width} r0,[r5]\nADDP #{stride},r4\nADDP #{stride},r5\n'+('SUBI.W #1,r1\n' if variant=='unit' else 'SUB r2,r1\n')+'BNE copy_loop\n'
            add(f'copy_{width}_{variant}',f'MOV.W #$4000,r4\nMOV.W #$4100,r5\nMOV.W #16,r1\n'+('MOV.W #1,r2\n' if variant=='hoisted' else '')+'copy_loop:\n'+loop+'HALT\n',[(0x4100,data)],[(0x4000,data),(0x4100,[0]*len(data))],family='post_increment',comparison=f'copy_{width}_unit' if variant=='hoisted' else None)
    for variant in ('branch','shifts'):
        # Both preserve B1 and I/E, clobber NZCV. Test every possible byte.
        code=('ANDI.B #255,r0\nCMPI.W #128,r0\nBCC sext_done\nORI.W #$FF00,r0\nsext_done:\n' if variant=='branch' else 'SHLI.W #8,r0\nSARI.W #8,r0\n')
        source='MOV.W #$4000,r4\nMOV.W #$4200,r5\nMOV.W #256,r1\nsext_loop:\nMOV.B [r4],r0\n'+code+'MOV.W r0,[r5]\nADDP #1,r4\nADDP #2,r5\nSUBI.W #1,r1\nBNE sext_loop\nHALT\n'
        add('sext_'+variant,source,[(0x4200,words([n if n<128 else n-256 for n in range(256)]))],[(0x4000,list(range(256))),(0x4200,[0]*512)],family='neg_sext',comparison='sext_branch' if variant=='shifts' else None)
    for variant in ('multiply','strength'):
        code='MOV.W #10,r1\nCALL multiply\n' if variant=='multiply' else 'MOV r0,r1\nSHLI.W #3,r0\nSHLI.W #1,r1\nADD r1,r0\n'
        add('scale10_'+variant,'MOV.W #1234,r0\n'+code+'MOV.W r0,[$4000]\nHALT\n',[(0x4000,words([12340]))],helpers=['multiply.asm'] if variant=='multiply' else [],family='multiply',comparison='scale10_multiply' if variant=='strength' else None)
    for kind in ('direct','indirect','relative_position'):
        driver='MOV.W #7,r0\n'+('MOV.W #@target@,r4\nCALL trampoline\n' if kind=='indirect' else 'CALL target\n')+'MOV.W r0,[$4000]\nHALT\n'
        target='target:\nADDI.W #9,r0\nRET\n'+('trampoline:\nJS r4\n' if kind=='indirect' else '')
        for origin in ((0x100,0x1200) if kind=='relative_position' else (0x100,)):
            add(f'call_{kind}_{origin}',driver+target,[(0x4000,words([16]))],family='call_relative_indirect',origin=origin,comparison='call_direct_256' if kind=='indirect' else None)
    for variant in ('conservative','live'):
        saves=list(range(4,8)) if variant=='conservative' else [4]
        source='MOV.W #99,r4\nCALL leaf\nMOV r4,r0\nMOV.W r0,[$4000]\nHALT\nleaf:\n'+''.join(f'PUSH r{n}\n' for n in saves)+'MOV.W #1,r4\n'+''.join(f'POP r{n}\n' for n in reversed(saves))+'RET\n'
        add('saves_'+variant,source,[(0x4000,words([99]))],family='pushm_popm',comparison='saves_conservative' if variant=='live' else None)
    desc='CALL $7054\nMOV.W #$6000,r4\nCALL $7000\nMOV.W r0,[$4000]\nMOV.W r1,[$4002]\nMOV.W #$4100,r5\nCALL readback\nMOV.W #4,r0\nMOV.W r0,[r4+4]\nCALL $7000\nMOV.W r0,[$4004]\nMOV.W #$4120,r5\nCALL readback\nMOV.W #$FFFF,r0\nMOV.W r0,[r4+6]\nCALL $7000\nMOV.W r0,[$4006]\nMOV.W r1,[$4008]\nMOV.W #$4140,r5\nCALL readback\nHALT\n'
    # Pinned K-only readback, I=0 and stable map ownership; no shared selectors.
    desc+='readback:\nMOV.W #7,r2\nMOV.W r2,[r5]\nMOV.W #3,r2\nMOV.W r2,[r5+2]\nSTC DESC,[r5]\nRET\n'
    record=[7,3,3,0x19,0,0,1,0,0,0]+[0]*6
    add('descriptor_lifecycle',desc,[(0x4000,words([0,1,0,1,0])),(0x4100,words([7,3,3,0x19,0,0,1,0,0])),(0x4120,words([7,3,4,0x19,0,0,1,0,0])),(0x4140,words([7,3,4,0x19,0,0,1,0,0]))],[(0x6000,words(record))],family='descriptor',controls=True)
    # Matched current-ISA implementations: identical generation/ownership ABI,
    # callers, payloads and pinning; one library uses scalar GETC/SETC sequences.
    cycle='CALL $7054\nMOV.W #$6000,r4\nCALL $7000\nMOV.W r0,[$4000]\nMOV.W #4,r0\nMOV.W r0,[r4+4]\nCALL $7000\nMOV.W r0,[$4002]\nCALL $705E\nMOV.W r0,[$4004]\nMOV.W [r4+4],r0\nMOV.W r0,[$4020]\nCALL $7007\nMOV.W r0,[$4006]\nCALL $705E\nMOV.W r0,[$4008]\nHALT\n'
    for scalar in (True,False):
        add('descriptor_cycle_'+('scalar' if scalar else 'bulk'),cycle,
            [(0x4000,words([0]*5)),(0x4020,words([4])),(0x6000,words([7,3]+[0]*8))],
            [(0x6000,words(record))],family='descriptor',controls=True,scalar_control_reference=scalar,
            comparison='descriptor_cycle_scalar' if not scalar else None)
    add('descriptor_cycle_absent_image',cycle,[(0x4000,words([0]*5)),(0x4020,words([4])),(0x6000,words([7,3]+[0]*8))],[(0x6000,words(record))],family='descriptor',controls=True,absent_image_reference=True,comparison='descriptor_cycle_bulk')
    # Equal helper ABI/error-status comparisons under the same working ISA.
    # These shims guard native faults; compiled / and % propagate cause eight.
    for original in list(result):
        if original['family'] not in ('multiply','divide_mod') or not original['helpers']:continue
        native=dict(original);native['name']=original['name']+'_native';native['comparison']=original['name']
        native['helpers']=[{'multiply.asm':'multiply_native.asm','divide.asm':'divide_native.asm','divide_simple.asm':'divide_native.asm','signed_divide.asm':'signed_divide_native.asm','decimal.asm':'decimal_native.asm'}[h] for h in original['helpers']]
        result.append(native)
    add('scale10_native','MOV.W #1234,r0\nMOV.W #10,r1\nMUL r1,r0\nMOV.W r0,[$4000]\nHALT\n',[(0x4000,words([12340]))],family='multiply',comparison='scale10_strength')
    for op,body in (('native','MOV.W #8,r1\nMUL r1,r0\n'),('shift','SHLI.W #3,r0\n')):
        add('scale8_'+op,'MOV.W #1234,r0\n'+body+'MOV.W r0,[$4000]\nHALT\n',[(0x4000,words([9872]))],family='multiply',comparison='scale8_native' if op=='shift' else None)
    # An explicitly bounded pointer scale: retain the same overflow guard before
    # using low-word MUL. This does not model arbitrary checked multiplication.
    for variant,body in (('loop','MOV r0,r2\nMOV.W #0,r0\nMOV.W #6,r3\nscale_loop:\nADD r2,r0\nSUBI.W #1,r3\nBNE scale_loop\n'),('native','MOV.W #6,r2\nMUL r2,r0\n')):
        add('checked_scale6_'+variant,'MOV.W #1000,r0\nCMPI.W #10922,r0\nBHI bad_scale\n'+body+'MOV.W r0,[$4000]\nHALT\nbad_scale:\nMOV.W #3,r0\nMOV.W r0,[$3F00]\nHALT\n',[(0x4000,words([6000]))],family='multiply',comparison='checked_scale6_loop' if variant=='native' else None)
    # Matched assigned-density alternatives: retain the same counter/flags,
    # helper ABI and caller except for the one instruction family named here.
    for original in list(result):
        name=original['name'];native=None
        if name in ('copy_B_unit','copy_W_unit'):
            for kind in ('dec','post'):
                native=dict(original);native['name']=name.removesuffix('_unit')+'_'+kind;native['comparison']=name
                width='B' if name.startswith('copy_B') else 'W';stride=1 if width=='B' else 2
                if kind=='dec':native['source']=original['source'].replace('SUBI.W #1,r1','DEC.W r1')
                else:native['source']=original['source'].replace(f'MOV.{width} [r4],r0\nMOV.{width} r0,[r5]\nADDP #{stride},r4\nADDP #{stride},r5',f'MOV.{width} [r4+],r0\nMOV.{width} r0,[r5+]')
                result.append(native)
        if name=='sext_shifts':
            native=dict(original);native['name']='sext_native';native['comparison']=name;native['source']=original['source'].replace('SHLI.W #8,r0\nSARI.W #8,r0','SEXT r0');result.append(native)
        if name.startswith('call_relative_position_'):
            native=dict(original);native['name']=name.replace('relative_position','relative_native');native['comparison']=name;native['source']=original['source'].replace('CALL target','CALLR target');result.append(native)
    return result

def generate(destination):
    out=Path(destination);out.parent.mkdir(parents=True,exist_ok=True);records=[]
    for spec in cases():
        source=spec['source']+'\n'+ '\n'.join((DIRECTORY/h).read_text() for h in spec['helpers'])
        rows=assemble(source.replace('@target@','0'),spec['origin'])
        symbols=image_symbols(source.replace('@target@','0'),rows,spec['origin'])
        if '@target@' in source:source=source.replace('@target@',str(symbols['target']));rows=assemble(source,spec['origin'])
        record=dict(spec,assembly=source,code=list(b''.join(b for _,b,_ in rows)),symbols=symbols,source_sha256=sha(source.encode()))
        record['instructions']=[]
        for pc,b,text in rows:
            e,v=decode(b);label=next((name for name,addr in reversed(list(symbols.items())) if addr<=pc),'driver')
            record['instructions'].append(dict(pc=pc,bytes=len(b),text=text,identifier=e['id'],family=e['semantic'],label=label))
        record['binary_sha256']=sha(bytes(record['code']));record['data_bytes']=sum(len(data) for _,data in spec['data'])
        if spec.get('scalar_control_reference') or spec.get('absent_image_reference'):
            library=(ROOT/'examples/kernel_controls.asm').read_text()
            selectors=('desc_map','desc_segment','desc_frame','desc_flags','desc_lower','desc_upper_lo','desc_upper_hi','desc_sb','desc_st')
            def scalar_install(base):
                return ''.join(f'MOV.W [{base}+{2*i}],r2\nSETC r2,{selector}\n' for i,selector in enumerate(selectors))+'MOV.W #1,r2\nSETC r2,command'
            if spec.get('scalar_control_reference'):library=library.replace('LDC [r4],DESC',scalar_install('r4'))
            scalar_store='MOV.W [r4],r2\nSETC r2,desc_map\nMOV.W [r4+2],r2\nSETC r2,desc_segment\n'
            scalar_store+=''.join(f'GETC ${0x30+i:02X},r2\nMOV.W r2,[r4+{4+2*i}]\n' for i in range(7))
            if spec.get('scalar_control_reference'):library=library.replace('STC DESC,[r4]',scalar_store.rstrip())
            else:
                first=library.index('do_descriptor_remove:');last=library.index('do_descriptor_readback:',first)
                library=library[:first]+(DIRECTORY/'absent_descriptor.asm').read_text()+library[last:]
            assembled=assemble(library,0x7000);labels=image_symbols(library,assembled,0x7000);_,registry=control_registry(assembled,labels)
            record['control_artifact']=dict(assembly=library,code=list(b''.join(b for _,b,_ in assembled)),registry=list(registry))
        records.append(record)
    vector=lambda data:'{'+','.join(map(str,data))+'}'
    pairs=lambda rows:'{'+','.join('{'+str(a)+','+vector(b)+'}' for a,b in rows)+'}'
    lines=['#pragma once','#include <string>','#include <vector>','#include <utility>','namespace density_images {','struct Program { std::string name; unsigned origin; bool controls; std::vector<unsigned char> code; std::vector<std::pair<unsigned,std::vector<unsigned char>>> data,checks; std::vector<unsigned char> control_code,registry; };','inline const std::vector<Program> programs = {']
    for r in records:lines.append('{'+f'{json.dumps(r["name"])},{r["origin"]},{str(r.get("controls",False)).lower()},'+vector(r['code'])+','+pairs(r['data'])+','+pairs(r['checks'])+','+vector(r.get('control_artifact',{}).get('code',[]))+','+vector(r.get('control_artifact',{}).get('registry',[]))+'},')
    lines+=['};','}'];out.write_text('\n'.join(lines)+'\n')
    paths=[Path(__file__),*sorted(DIRECTORY.glob('*.asm')),DIRECTORY/'adoption.json',DIRECTORY/'census.hpp',
           ROOT/'tests/density_census.cpp',ROOT/'CMakeLists.txt',ROOT/'src/cpu.cpp',ROOT/'src/cpu.hpp',ROOT/'src/memory_transaction.hpp',ROOT/'src/protected.cpp',ROOT/'src/replay.cpp',
           ROOT/'tools/isa.py',ROOT/'tools/encoding.py',ROOT/'tools/generate_kernel_fixtures.py',ROOT/'doc/ISA_ENCODING.md',ROOT/'examples/console_guest.hpp',
           *(ROOT/f'examples/kernel_{n}.asm' for n in IMAGES)]
    manifest={'isa':'working','seed':47017,'programs':records,'sources':{str(p.relative_to(ROOT)):sha(p.read_bytes()) for p in paths}}
    text=json.dumps(manifest,indent=2)+'\n';out.with_suffix('.json').write_text(text)
    with out.open('a') as f:f.write('namespace density_images { inline const char* manifest_sha256 = '+json.dumps(sha(text.encode()))+'; }\n')
if __name__=='__main__':generate(sys.argv[1])
