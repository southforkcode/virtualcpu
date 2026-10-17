#!/usr/bin/env python3
"""Flat-image assembler/disassembler for the working ISA."""
from __future__ import annotations
import argparse
from dataclasses import dataclass
from pathlib import Path
import re
from encoding import metadata, tables, ROOT

META = metadata()
TABLES = tables()
BANKS = META['register_banks']
ENTRIES = {e['id']: (name, e) for name, t in META['tables'].items() for e in t['entries'] if 'dispatch' not in e}
PREFIXES = {e['dispatch']: e['match']['value'] for e in META['tables']['primary']['entries'] if 'dispatch' in e}
BRANCHES = list(dict.fromkeys(e['mnemonic'] for e in META['tables']['primary']['entries'] if e.get('semantic', '').startswith('branch_')))


def require(ok, message):
    if not ok:
        raise ValueError(message)


def number(text):
    text = text.strip()
    require(re.fullmatch(r'[+-]?(?:\$[0-9a-fA-F]+|0x[0-9a-fA-F]+|[0-9]+)',text) is not None,'Invalid number: '+text)
    sign = -1 if text.startswith('-') else 1
    text = text.lstrip('+-')
    return sign * int(text[1:], 16) if text.startswith('$') else sign * int(text, 0 if text.startswith('0x') else 10)


@dataclass(frozen=True)
class Instruction:
    text: str
    mnemonic: str
    args: tuple[str, ...]


def parse(source):
    items = []
    for line in source.splitlines():
        line = line.split(';')[0].strip()
        if not line:
            continue
        if ':' in line and re.match(r'^\w+:', line):
            label, line = line.split(':', 1)
            items.append(label)
            line = line.strip()
            if not line:
                continue
        parts = line.split(None, 1)
        items.append(Instruction(line, parts[0].upper(), tuple(a.strip().lower() for a in parts[1].split(',')) if len(parts)>1 else ()))
    return items


def emit(identifier, fields=None, operand=0):
    fields = fields or {}
    table, e = ENTRIES[identifier]
    m = e['match']
    data = bytearray(e['length'])
    pos = META['tables'][table]['inspect_byte']
    data[pos] = m.get('value', m.get('values', [0])[0])
    if table != 'primary':
        data[0] = PREFIXES[table]
    for f in e['fields']:
        val = fields[f['name']]
        if isinstance(val, str):
            require(val in BANKS[f['bank']], f"Invalid {f['bank']} register {val}")
            val = BANKS[f['bank']].index(val)
        mask = (1 << f['width'])-1
        require(0 <= val <= mask, 'Field out of range')
        data[f['byte']] = (data[f['byte']] & ~(mask << f['lsb'])) | val << f['lsb']
    for o in e.get('bit_operands', []):
        bits = o['width']; mask = (1 << bits)-1
        low = -(1 << (bits-1)) if o['signed'] else 0
        high = (1 << (bits-1))-1 if o['signed'] else mask
        require(low <= operand <= high, f'Operand out of range: {operand}')
        data[o['byte']] = (data[o['byte']] & ~(mask << o['lsb'])) | (operand & mask) << o['lsb']
    for o in e['operands']:
        bits = 8*o['width_bytes']
        low = -(1 << (bits-1)) if o['signed'] or e['semantic'] in ('move_immediate_zero_extend','test_immediate','alu_immediate') else 0
        high = (1 << (bits-1))-1 if o['signed'] else (1 << bits)-1
        require(low <= operand <= high, f'Operand out of range: {operand}')
        data[o['byte']:o['byte']+o['width_bytes']] = (operand & ((1 << bits)-1)).to_bytes(o['width_bytes'], 'little')
    decoded, _ = decode(bytes(data))
    require(decoded['id']==identifier, 'Encoding mismatch')
    return bytes(data)


def encode(ins, pc, labels):
    op, args = ins.mnemonic, ins.args
    aliases = {a['mnemonic']: a['expands_to'] for a in META['aliases'] if 'adjustment' not in a and 'immediate' not in a and not a.get('same_general_register_operands') and not a.get('same_b0_register_operands')}
    op = aliases.get(op, op)
    if op in ('INCP','DECP'):
        require(len(args)==1, 'Pointer alias needs one destination')
        return encode(Instruction(ins.text,'ADDP',('#1' if op=='INCP' else '#-1', args[0])),pc,labels)
    def imm(text):
        require(text.startswith('#'), 'Expected immediate')
        return number(text[1:])
    def target(text):
        return labels[text] if text in labels else number(text)
    family=op.split('.')[0]
    if family=='SEXT':
        require(op in ('SEXT','SEXT.W') and len(args)==1,'Expected one B0 sign-extension operand')
        return emit('sext',{'destination':args[0]})
    if family in ('INC','DEC'):
        require(op in (family,family+'.W',family+'.B') and len(args)==1, 'Expected one B0 unit operand')
        if op.endswith('.B'):return emit(('addi' if family=='INC' else 'subi')+'_8',{'destination':args[0]},1)
        return emit(family.lower()+'_16',{'destination':args[0]})
    if family in ('MUL','DIVU','DIVS','MODU','MODS'):
        require(op in (family,family+'.W') and len(args)==2, 'Expected two word B0 operands')
        return emit(family.lower(),{'source':args[0],'destination':args[1]})
    if family in ('LDX','STX'):
        require(op==family and len(args)==1, 'Expected context image B1 pointer')
        match=re.fullmatch(r'\[(r[4-7])\]',args[0].replace(' ',''))
        require(match is not None, 'Context image requires a plain B1 pointer')
        return emit(op.lower()+'_ctx',{'base':match[1]})
    if family in ('LDC','STC'):
        require(op==family and len(args)==2, 'Expected descriptor image and B1 pointer')
        memory,kind=args if op=='LDC' else args[::-1]
        require(kind=='desc', 'Only DESC/1 control images are assigned')
        match=re.fullmatch(r'\[(r[4-7])\]',memory.replace(' ',''))
        require(match is not None, 'Descriptor image requires a plain B1 pointer')
        return emit(op.lower()+'_desc',{'base':match[1]})
    if family in ('ADDI','SUBI','CMPI','ANDI','ORI','XORI'):
        require(op in (family,family+'.B',family+'.W') and len(args)==2, 'Invalid immediate ALU operands/width')
        return emit(f'{family.lower()}_{8 if op.endswith(".B") else 16}', {'destination':args[1]}, imm(args[0]))
    if op in ('GETC','SETC'):
        require(len(args)==2, 'Expected control and register')
        control,reg=args if op=='GETC' else args[::-1]
        selector=META['control_indices'].get(control)
        if selector is None: selector=number(control)
        return emit(op.lower(), {'destination' if op=='GETC' else 'source':reg}, selector)
    if family=='MOVU':
        require(op in ('MOVU','MOVU.B','MOVU.W') and len(args)==2, 'Invalid MOVU operands/width')
        store=args[1].startswith('['); memory,reg=(args[1],args[0]) if store else args
        match=re.fullmatch(r'\[(r[4-7])([+-].+)?\]',memory.replace(' ',''))
        require(match is not None, 'MOVU requires a B1 base without segment override')
        base,disp=match.groups(); displacement=number(disp) if disp else 0
        return emit(f'movu_{"disp16" if displacement else "direct"}_{"store" if store else "load"}_{8 if op.endswith(".B") else 16}',
                    {'source' if store else 'destination':reg,'base':base},displacement)
    if op.split('.')[0] in ('SHLI','SHRI','SARI','ROLI','RORI'):
        mnemonic=op.split('.')[0]
        require(op in (mnemonic,mnemonic+'.B',mnemonic+'.W'), 'Invalid shift/rotate width')
        require(len(args)==2, 'Expected immediate count and B0 destination')
        count=imm(args[0])
        require(0<=count<=16, 'Shift/rotate count must be 0..16')
        bits=8 if op.endswith('.B') else 16
        return emit(f'{mnemonic.lower()}_{bits}', {'destination':args[1]}, count)
    if op in ('TST','TST.W','TST.B','TSTI','TSTI.W','TSTI.B'):
        bits = 8 if op.endswith('.B') else 16
        immediate = op.startswith('TSTI')
        require(len(args)==(2 if immediate else 1), 'Wrong test operand count')
        reg = args[-1]
        require(reg in BANKS['B0'], 'Test requires a B0 register')
        if not immediate and bits==16:
            return emit('or', {'source':reg,'destination':reg})
        identifier = f'tsti_{bits}' if immediate else 'tst_8'
        return emit(identifier, {'destination':reg}, imm(args[0]) if immediate else 0)
    if op in ('NOP','HALT','DI','EI','RET','RETF','IRET'):
        require(not args, 'Unexpected operand')
        return emit(op.lower())
    if family in BRANCHES or family=='CALLR':
        require(op in (family,family+'.S',family+'.W') and len(args)==1, 'Invalid branch operands/width')
        short=op.endswith('.S')
        return emit(family.lower()+('_short' if short else ''), operand=target(args[0])-pc-(2 if short else 3))
    if op in ('ADDP','ADJSP','SWI'):
        require(len(args)==(2 if op=='ADDP' else 1), 'Wrong operand count')
        identifier = 'addp_'+args[1] if op=='ADDP' else op.lower()
        require(identifier in ENTRIES, 'Invalid pointer destination')
        return emit(identifier, operand=imm(args[0]))
    if op in ('PUSH','POP','SHL','SHR','SAR'):
        require(len(args)==1, 'Expected one register')
        return emit(op.lower(), {'source' if op=='PUSH' else 'destination':args[0]})
    if op in ('CALL','CALLF','JS','JL'):
        require(len(args)==1, 'Expected one target')
        if op=='CALLF' and args[0] in labels:
            require(0<=labels[args[0]]<=65535, 'Far-call label is outside segment zero')
        if op=='JS' and args[0] in BANKS['B1']:
            return emit('js_register', {'target':args[0]})
        value = target(args[0])
        require(0<=value<=(0xffffff if op in ('JL','CALLF') else 65535), 'Invalid target')
        return emit({'CALL':'call','CALLF':'callf','JS':'js_absolute','JL':'jl'}[op], operand=value)
    if op in ('GETX','SETX'):
        require(len(args)==2, 'Expected context and register')
        context, reg = args if op=='GETX' else args[::-1]
        index=META['context_indices'].get(context)
        if index is None: index=number(context)
        if index>=0x20:
            return emit(op.lower()+'_extended', {'destination' if op=='GETX' else 'source':reg},index)
        require(0<=index<21, 'Unknown context')
        context=BANKS['context'][index]
        if op=='SETX': require(index<17, 'Context metadata is read-only')
        return emit(op.lower(), {'context':context, 'destination' if op=='GETX' else 'source':reg})
    if op in ('XOR','AND','OR','ADC','SBC','ADD','SUB','CMP'):
        require(len(args)==2, 'Expected two accumulators')
        return emit(op.lower(), {'source':args[0], 'destination':args[1]})
    require(op in ('MOV','MOV.W','MOV.B') and len(args)==2, 'Unsupported instruction: '+ins.text)
    src,dst=args
    bits = 8 if op=='MOV.B' else 16
    if src.startswith('#'):
        return emit(f'mov_immediate_{bits}', {'destination':dst}, imm(src))
    if src.startswith('[') or dst.startswith('['):
        store = dst.startswith('[')
        memory, reg = (dst,src) if store else (src,dst)
        require(memory.endswith(']'), 'Invalid memory operand')
        inner=memory[1:-1].replace(' ','')
        post=re.fullmatch(r'(r[4-7])\+',inner)
        if post:
            return emit(f'mov_post_{"store" if store else "load"}_{bits}',{'source' if store else 'destination':reg,'base':post[1]})
        pointer = re.fullmatch(r'(?:(ds|cs):)?(r[4-7])([+-].+)?',inner)
        fields={'source' if store else 'destination':reg}
        direction='store' if store else 'load'
        if pointer:
            segment,base,disp=pointer.groups(); value=number(disp) if disp else 0
            fields['base']=base
            short = -32 <= value <= 31
            identifier=f'mov_b1_{segment or "ds"}_{"direct" if short else "disp16"}_{direction}_{bits}'
        else:
            match=re.fullmatch(r'(dp|sp)([+-].+)?',inner)
            base=match[1] if match else 'absolute'; value=number(match[2] or '0') if match else number(inner)
            identifier=f'mov_{base}_{direction}_{bits}'
            if base=='sp' and 0<=value<=255:
                identifier=f'mov_sp8_{direction}_{bits}'
        return emit(identifier,fields,value)
    require(op!='MOV.B', 'Register MOV is word only')
    if src==dst and src in BANKS['general']:
        return emit('nop')
    if src.startswith('~'):
        return emit('mov_not',{'source':src[1:],'destination':dst})
    def bank(reg):
        for b in ('B0','B1','B2','B3'):
            if reg in BANKS[b]: return b.lower()
        raise ValueError('Unknown register: '+reg)
    sb,db=bank(src),bank(dst)
    identifier='mov_acc' if sb==db=='b0' else f'mov_{sb}_{db}'
    require(identifier in ENTRIES, 'Transfer needs an explicit accumulator temporary')
    return emit(identifier, {'source':src,'destination':dst})


def assemble(source, origin=0x100):
    items=parse(source); initial={}; branches={}; label_items={}
    for i,item in enumerate(items):
        if isinstance(item,str):
            name=item.lower();require(name not in label_items,'Duplicate label');label_items[name]=i
            continue
        family=item.mnemonic.split('.')[0]
        if family in BRANCHES or family=='CALLR':
            require(item.mnemonic in (family,family+'.S',family+'.W') and len(item.args)==1,'Invalid branch operands/width')
            branches[i]=item;initial[i]=2 if item.mnemonic.endswith('.S') else 3
        elif item.mnemonic in ('CALL','CALLF','JS','JL'):
            require(len(item.args)==1,'Expected one target')
            initial[i]=2 if item.mnemonic=='JS' and item.args[0] in BANKS['B1'] else {'CALL':4,'CALLF':5,'JS':3,'JL':5}[item.mnemonic]
        else:initial[i]=len(encode(item,origin,{}))
    def layout(sizes):
        pc=origin;labels={};positions={}
        for i,item in enumerate(items):
            if isinstance(item,str):labels[item.lower()]=pc
            else:positions[i]=pc;pc+=sizes[i]
        return labels,positions,pc
    automatic={i for i,item in branches.items() if item.mnemonic in BRANCHES or item.mnemonic=='CALLR'}
    pinned=set()
    while True:
        sizes=dict(initial)
        while True:
            labels,positions,end=layout(sizes);shrink=[]
            for i in sorted(automatic-pinned):
                if sizes[i]==2:continue
                target=branches[i].args[0]
                # A forward label follows this branch and moves by its own
                # shrink; an absolute numeric target never moves.
                destination=labels[target]-(label_items[target]>i) if target in labels else number(target)
                if -128<=destination-positions[i]-2<=127:shrink.append(i)
            if not shrink:break
            for i in shrink:sizes[i]=2
        # Shrinking earlier code can move a numeric target out of rel8 reach.
        # Pin those automatic branches long and restart; pins only accumulate,
        # so this terminates without oscillation. Label-only passes only shrink.
        invalid={i for i in automatic if sizes[i]==2 and branches[i].args[0] not in labels and
                 not -128<=number(branches[i].args[0])-positions[i]-2<=127}
        if not invalid:break
        pinned|=invalid
    require(0<=origin<=65535 and end<=65536,'Image crosses code segment')
    rows=[]
    for i,item in enumerate(items):
        if isinstance(item,str):continue
        selected=item
        if i in branches:
            family=item.mnemonic.split('.')[0]
            selected=Instruction(item.text,family+('.S' if sizes[i]==2 else '.W'),item.args)
        data=encode(selected,positions[i],labels)
        require(len(data)==sizes[i],'Assembler layout mismatch')
        rows.append((positions[i],data,item.text))
    return rows


def decode(data):
    require(bool(data), 'Missing opcode')
    byte=data[0]; e=TABLES['primary'][byte]
    require(e is not None, 'Illegal opcode')
    if 'dispatch' in e:
        require(len(data)>1, 'Missing extension byte'); byte=data[1]; e=TABLES[e['dispatch']][byte]
        require(e is not None, 'Illegal extension')
    for i in range(e['length']):
        require(i<len(data), 'Truncated instruction')
        for c in e.get('encoding_checks',[]):
            if c['byte']==i:
                require(data[i] in c['allowed_values'] if 'allowed_values' in c else data[i]&c['mask']==c['equals'], 'Reserved encoding bits/selector')
    values={f['name']:BANKS[f['bank']][(data[f['byte']]>>f['lsb'])&((1<<f['width'])-1)] for f in e['fields']}
    for o in e.get('bit_operands', []):
        value = (data[o['byte']] >> o['lsb']) & ((1 << o['width'])-1)
        if o['signed'] and value & (1 << (o['width']-1)):
            value -= 1 << o['width']
        values[o['name']] = value
    for o in e['operands']:
        values[o['name']]=int.from_bytes(data[o['byte']:o['byte']+o['width_bytes']], 'little', signed=o['signed'])
    return e,values


def disassemble(data, pc=0x100):
    e,v=decode(data); name=e['id']; op=e['mnemonic']; args=[]
    def literal(value): return f'${value:X}' if value>=0 else f'-${-value:X}'
    if e['semantic'].startswith('branch_') or e['semantic']=='call_near_relative':
        op+='.S' if e['length']==2 else '.W'
        args=[literal(pc+e['length']+v['displacement'])]
    elif name in ('ldx_ctx','stx_ctx'): args=['['+v['base']+']']
    elif name in ('ldc_desc','stc_desc'):
        memory='['+v['base']+']'
        args=[memory,'DESC'] if name=='ldc_desc' else ['DESC',memory]
    elif name.startswith('addp_'): args=['#'+str(v['adjustment']),e['destination']]
    elif name=='adjsp': args=['#'+str(v['adjustment'])]
    elif name=='swi': args=['#'+str(v['service'])]
    elif name in ('getc','setc','getx_extended','setx_extended'):
        names=META['control_indices'] if name in ('getc','setc') else META['context_indices']
        selector=next(n for n,i in names.items() if i==v['selector'])
        args=[selector,v['destination']] if name.startswith('get') else [v['source'],selector]
    elif e['semantic']=='unit_arithmetic':op+='.W';args=[v['destination']]
    elif e['semantic']=='alu_immediate':
        op+='.'+('B' if e['operation_bits']==8 else 'W'); args=['#'+literal(v['immediate']),v['destination']]
    elif name=='getx': args=[v['context'],v['destination']]
    elif name=='setx': args=[v['source'],v['context']]
    elif e['semantic']=='shift_rotate_immediate':
        op+='.'+('B' if e['operation_bits']==8 else 'W')
        args=['#'+str(v['count']),v['destination']]
    elif e['semantic'] in ('test_register','test_immediate'):
        op+='.'+('B' if e['operation_bits']==8 else 'W')
        args=(['#'+literal(v['immediate'])] if e['semantic']=='test_immediate' else [])+[v['destination']]
    elif 'address' in e:
        a=e['address']; base=a['base']
        if base=='absolute': mem=literal(v['offset'])
        else:
            disp=v.get('displacement',v.get('offset',0)); b=v['base'] if base=='B1' else base
            mem=(a['segment']+':' if base=='B1' and a['segment']!='uaccess' else '')+b+(('+' if disp>=0 else '-')+literal(abs(disp)) if disp else '')
        if a.get('post_increment'):mem=v['base']+'+'
        memory='['+mem+']'; args=[v['source'],memory] if 'source' in v else [memory,v['destination']]
        op+='.'+('B' if e['transfer_bits']==8 else 'W')
    elif name.startswith('mov_immediate'): op+='.'+('B' if e['transfer_bits']==8 else 'W'); args=['#'+literal(v['immediate']),v['destination']]
    elif 'source' in v and 'destination' in v: args=[('~' if name=='mov_not' else '')+v['source'],v['destination']]
    elif 'source' in v: args=[v['source']]
    elif 'destination' in v: args=[v['destination']]
    elif 'target' in v: args=[v['target'] if isinstance(v['target'],str) else literal(v['target'])]
    return op+(' '+','.join(args) if args else ''),e['length']


def main():
    p=argparse.ArgumentParser(description=__doc__); p.add_argument('action',choices=['assemble','disassemble']); p.add_argument('input',type=Path)
    p.add_argument('--origin',type=lambda x:int(x,0),default=0x100); p.add_argument('-o','--output',type=Path)
    a=p.parse_args()
    if a.action=='assemble':
        listing=assemble(a.input.read_text(),a.origin)
        if a.output: a.output.write_bytes(b''.join(row[1] for row in listing))
        else:
            for pc,data,text in listing: print(f'{pc:04X}  {data.hex(" ").upper():<14} {text}')
    else:
        data=a.input.read_bytes(); pos=0; lines=[]
        while pos<len(data):
            text,length=disassemble(data[pos:],a.origin+pos); lines.append(text); pos+=length
        result='\n'.join(lines)+'\n'
        if a.output: a.output.write_text(result)
        else: print(result,end='')

if __name__=='__main__': main()
