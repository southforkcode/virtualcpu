import re
import copy
import sys
import unittest
from unittest.mock import patch
from pathlib import Path
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'tools'))
from isa import assemble, decode, disassemble, TABLES, ROOT
from encoding import metadata, validate


class Toolchain(unittest.TestCase):
    def test_current_instruction_contract(self):
        self.assertEqual(metadata()['isa_revision'],'working')
        self.assertEqual(metadata()['schema_version'],5)
        samples = ('SWI #1','GETX xtrap,r0','TST.B r0','TSTI.W #32768,r1',
                   'ADDI.B #1,r0','CALLF $10200','RETF','GETC process_map,r0',
                   'MOV.W [r4+2],r0','MOV.W [sp+255],r0','SHLI.W #8,r0')
        for source in samples:
            with self.subTest(source=source):
                data=assemble(source)[0][1]
                text,length=disassemble(data)
                self.assertEqual(length,len(data))
                self.assertEqual(assemble(text)[0][1],data)

    def test_documentation_status_and_links(self):
        paths=[ROOT/'README.md',*sorted((ROOT/'doc').glob('*.md'))]
        references=[ROOT/'README.md',ROOT/'doc/ISA.md',ROOT/'doc/ISA_ENCODING.md',ROOT/'doc/ISA_EXAMPLES.md']
        self.assertEqual({p.name for p in (ROOT/'doc').iterdir() if p.is_file()},
                         {'ISA.md','ISA_ENCODING.md','ISA_EXAMPLES.md'})
        for path in references:
            self.assertIn('implementation-status-and-remaining-work',path.read_text(),str(path))
        manual=(ROOT/'doc/ISA.md').read_text()
        self.assertEqual(manual.count('<!-- ISA_IMPLEMENTATION_STATUS_BEGIN -->'),1)
        self.assertEqual(manual.count('<!-- ISA_IMPLEMENTATION_STATUS_END -->'),1)
        def prose(text):
            result=[]; fenced=False
            for line in text.splitlines():
                if line.startswith('```'): fenced=not fenced
                elif not fenced: result.append(line)
            self.assertFalse(fenced,'Unbalanced code fence')
            return '\n'.join(result)
        def anchors(text):
            found=set(); counts={}
            for title in re.findall(r'^#{1,6} (.+)$',prose(text),re.M):
                base=re.sub(r'[^\w -]','',title.replace('`','').replace('*','').lower()).replace(' ','-')
                count=counts.get(base,0); counts[base]=count+1
                found.add(base+(f'-{count}' if count else ''))
            return found
        for path in paths:
            for target in re.findall(r'\]\(([^)]+)\)',prose(path.read_text())):
                if '://' in target: continue
                filename,_,fragment=target.partition('#')
                destination=(path.parent/filename).resolve() if filename else path
                self.assertTrue(destination.exists(),f'{path}: {target}')
                if fragment and destination.suffix=='.md':
                    self.assertIn(fragment,anchors(destination.read_text()),f'{path}: {target}')
        # Check the printed timer setup and ACK as actual assembly, without
        # claiming the illustrative multi-image copy ABI is a runnable fixture.
        timer=manual.split('### Timer setup and acknowledgment example\n',1)[1].split('\n### ',1)[0]
        blocks=re.findall(r'```asm\n(.*?)```',timer,re.S)
        self.assertEqual(len(blocks),2)
        for source in blocks:
            for pc,data,_ in assemble(source):
                formatted,length=disassemble(data,pc)
                self.assertEqual(length,len(data))
                self.assertEqual(assemble(formatted,pc)[0][1],data)

    def test_maps(self):
        # The normative allocation ledger sets each assigned table count.
        manual=(ROOT/'doc/ISA.md').read_text()
        labels={'primary':'Working primary map','ext10':'`ext10`','ext15':'`ext15`',
                'ext1a':'`ext1a`','ext1f':'Working `ext1f`','ext27':'Working `ext27`','ext28':'Working `ext28`'}
        for name,rows in TABLES.items():
            line=next(line for line in manual.splitlines() if line.startswith('| '+labels[name]+' |'))
            count=int(line.split('|')[3].strip().split(',')[0])
            self.assertEqual(sum(e is not None for e in rows),count,name)

    def test_metadata_validation(self):
        for mutate in (
            lambda m: m.update(schema_version=2),
            lambda m: m.update(instruction_bytes={'min':1,'max':6}),
            lambda m: m['tables']['primary']['entries'].append(copy.deepcopy(m['tables']['primary']['entries'][0])),
            lambda m: m['tables']['primary']['entries'][0].update(length=6),
            lambda m: m['tables']['primary']['entries'][0].update(match={'mask':0,'value':1}),
            lambda m: m['tables']['ext27']['entries'][0]['fields'][0].update(byte=5),
            lambda m: m['tables']['ext27']['entries'][0]['encoding_checks'][0].update(byte=5),
            lambda m: next(e for e in m['tables']['ext1f']['entries'] if e['id']=='setc')['encoding_checks'][0].update(allowed_values=[256]),
        ):
            data=copy.deepcopy(metadata()); mutate(data)
            with self.assertRaises(ValueError): validate(data)

    def test_compact_b1(self):
        # Exact bytes for every short descriptor/secondary value and fallback.
        for cs in (False,True):
            for store in (False,True):
                for byte in (False,True):
                    for reg in range(8):
                        tag=0x80|(cs<<5)|(store<<6)|(byte<<3)|reg
                        for base in range(4):
                            for disp in range(-33,33):
                                memory=f'[{"cs" if cs else "ds"}:r{4+base}{disp:+d}]'
                                source=f'MOV.{"B" if byte else "W"} '+(f'r{reg},{memory}' if store else f'{memory},r{reg}')
                                short=-32<=disp<=31
                                data=bytes((0x15,tag,((disp&63)<<2)|base)) if short else bytes((0x15,tag|16,base))+(disp&65535).to_bytes(2,'little')
                                self.assertEqual(assemble(source)[0][1],data)
                                e,v=decode(data); self.assertEqual(v['displacement'],disp)
                                text,length=disassemble(data)
                                self.assertEqual(length,len(data)); self.assertEqual(assemble(text)[0][1],data)
                            for disp in (-32,0,31):
                                raw=bytes((0x15,tag|16,base))+(disp&65535).to_bytes(2,'little')
                                self.assertEqual(decode(raw)[1]['displacement'],disp)
                                text,length=disassemble(raw); self.assertEqual(length,5)
                                self.assertEqual(len(assemble(text)[0][1]),3)
        # Reject unsupported schemas and invalid packed fields.
        def packed(m):
            return next(e for e in m['tables']['ext15']['entries'] if e.get('bit_operands'))
        for mutate in (
            lambda m:m.update(schema_version=3),
            lambda m:packed(m)['bit_operands'][0].update(width=7),
            lambda m:packed(m)['bit_operands'][0].update(lsb=0),
            lambda m:packed(m)['bit_operands'][0].update(signed=1),
            lambda m:packed(m)['bit_operands'][0].update(name='base'),
            lambda m:packed(m)['bit_operands'][0].update(byte=5),
        ):
            data=copy.deepcopy(metadata());mutate(data)
            with self.assertRaises(ValueError):validate(data)
        for short_tag in (0,0x20,0x80,0xa0):
            with self.assertRaisesRegex(ValueError,'Reserved'):decode(bytes((0x27,short_tag,4)))
        self.assertEqual(assemble('MOV.W [r4+2],r0')[0][1],bytes((0x15,0x80,8)))

    def test_immediate_alu(self):
        for kind,op in enumerate(('ADDI','SUBI','CMPI','ANDI','ORI','XORI')):
            for bits in (8,16):
                for reg in range(4):
                    for value in (-(1<<(bits-1)),-1,0,1,1<<(bits-1),(1<<bits)-1):
                        source=f'{op}.{"B" if bits==8 else "W"} #{value},r{reg}'
                        data=bytes([0x80+kind*8+(4 if bits==16 else 0)+reg])+(value&((1<<bits)-1)).to_bytes(bits//8,'little')
                        self.assertEqual(assemble(source)[0][1],data)
                        text,length=disassemble(data)
                        self.assertEqual(length,len(data)); self.assertEqual(assemble(text)[0][1],data)
                        for n in range(len(data)):
                            with self.assertRaises(ValueError): decode(data[:n])
                self.assertEqual(assemble(f'{op} #1,r0')[0][1],bytes([0x84+kind*8,1,0]))
                for source in (f'{op}.Q #1,r0',f'{op}.B #256,r0',f'{op}.B #-129,r0',f'{op}.W #65536,r0',
                               f'{op}.W #-32769,r0',f'{op}.B #1,r4',f'{op}.B #1,sp',f'{op}.B r0,r1'):
                    with self.assertRaises(ValueError): assemble(source)

    def test_controls_context(self):
        m=metadata()
        for op,names,writable,tags in (
            ('GETC',m['control_indices'],None,0xa0),('SETC',m['control_indices'],set(m['control_writable_indices']),0xb0),
            ('GETX',m['context_indices'],None,0x80),('SETX',m['context_indices'],set(m['context_writable_indices']),0x90)):
            for name,index in names.items():
                for reg in range(8):
                    source=f'{op} {name},r{reg}' if op.startswith('GET') else f'{op} r{reg},{name}'
                    permitted=(op!='GETC' or name!='command') and (writable is None or index in writable)
                    if not permitted:
                        with self.assertRaises(ValueError): assemble(source)
                        continue
                    if op.endswith('X') and index<0x20:
                        data=bytes([0x1f,(0x20 if op=='GETX' else 0x40)+index,reg])
                    else: data=bytes([0x1f,tags+reg,index])
                    self.assertEqual(assemble(source)[0][1],data)
                    numeric=f'{op} {index},r{reg}' if op.startswith('GET') else f'{op} r{reg},{index}'
                    self.assertEqual(assemble(numeric)[0][1],data)
                    text,length=disassemble(data); self.assertEqual(length,3)
                    self.assertEqual(assemble(text)[0][1],data)
                    for n in range(3):
                        with self.assertRaises(ValueError): decode(data[:n])
        for tag,allowed in ((0x80,set(range(0x20,0x2a))|set(range(0x30,0x3b))),
                            (0x90,{0x20,0x21,0x22}),(0xa0,set(m['control_indices'].values())-{127}),
                            (0xb0,set(m['control_writable_indices']))):
            for index in range(256):
                if index in allowed: decode(bytes([0x1f,tag,index]))
                else:
                    with self.assertRaisesRegex(ValueError,'Illegal|Reserved'): decode(bytes([0x1f,tag,index]))
        for source in ('GETC mode,r8','GETC mode,sp','SETC r0,256','GETX 255,r0','SETX r0,xorigin_mode','GETC.W mode,r0'):
            with self.assertRaises(ValueError): assemble(source)

    def test_movu(self):
        for store in (False,True):
            for bits in (8,16):
                for reg in range(8):
                    for base in range(4):
                        for disp in (-32768,-1,0,1,32767):
                            memory=f'[r{4+base}'+(f'{disp:+}' if disp else '')+']'
                            source=f'MOVU.{"B" if bits==8 else "W"} '+(f'r{reg},{memory}' if store else f'{memory},r{reg}')
                            tag=(128 if store else 0)|(32 if bits==8 else 0)|(64 if disp else 0)|(reg<<2)
                            data=bytes([0x27,tag,base])+((disp&65535).to_bytes(2,'little') if disp else b'')
                            self.assertEqual(assemble(source)[0][1],data)
                            text,length=disassemble(data); self.assertEqual(length,len(data))
                            self.assertEqual(assemble(text)[0][1],data)
                            for n in range(len(data)):
                                with self.assertRaises(ValueError): decode(data[:n])
        for byte in range(256):
            for base in (0,3,4,255):
                raw=bytes([0x27,byte,base,0,0])
                if byte&0x83==byte&0x80 and base<4: decode(raw)
                else:
                    with self.assertRaisesRegex(ValueError,'Illegal|Reserved'): decode(raw)
        text,length=disassemble(bytes.fromhex('27 40 00 00 00'))
        self.assertEqual(length,5); self.assertEqual(assemble(text)[0][1],bytes.fromhex('27 00 00'))
        for source in ('MOVU.B [sp],r0','MOVU [ds:r4],r0','MOVU [r0],r0','MOVU #1,r0',
                       'MOVU r0,r1','MOVU [r4],sp','MOVU.Q [r4],r0','MOVU [r4+32768],r0','MOVU [r4-32769],r0'):
            with self.assertRaises(ValueError): assemble(source)

    def test_kernel_images(self):
        from generate_kernel_fixtures import IMAGES, generate
        self.assertIn('namespace kernel_images',generate())
        for name,origin in IMAGES.items():
            rows=assemble((ROOT/f'examples/kernel_{name}.asm').read_text(),origin)
            for pc,code,_ in rows:
                text,length=disassemble(code,pc)
                self.assertEqual(length,len(code)); self.assertEqual(assemble(text,pc)[0][1],code)

    def test_console_guest_contract(self):
        from generate_kernel_fixtures import IMAGES, image_symbols
        source=(ROOT/'examples/kernel_console.asm').read_text()
        rows=assemble(source,IMAGES['console'])
        labels=image_symbols(source,rows,IMAGES['console'])
        self.assertEqual(labels['irq'],0x8000)
        self.assertLess(rows[-1][0]+len(rows[-1][1]),0xa000)
        memory=(ROOT/'examples/kernel_console_memory.asm').read_text()
        self.assertIn(f'CMPI.W #{labels["copy_fault"]},r0',memory)
        self.assertIn(f'MOV.W #{labels["copy_error"]},r0',memory)
        self.assertIn(f'CMPI.W #{labels["idle"]},r0',source)
        self.assertIn(f'MOV.W #{labels["idle_resume"]},r0',source)
        for name in ('console_user','console_peer'):
            image=assemble((ROOT/f'examples/kernel_{name}.asm').read_text(),0x100)
            self.assertLessEqual(image[-1][0]+len(image[-1][1]),0x200)
        for name in ('console','console_boot','console_memory'):
            self.assertNotIn('SETC ',(ROOT/f'examples/kernel_{name}.asm').read_text())

    def test_kernel_control_contract(self):
        from generate_kernel_fixtures import image_symbols, control_registry
        source=(ROOT/'examples/kernel_controls.asm').read_text()
        rows=assemble(source,0x7000); symbols=image_symbols(source,rows,0x7000)
        public=('descriptor_install','descriptor_remove','map_publish','map_invalidate','entry_configure',
                'backing_query','uaccess_begin','uaccess_end','prepare_user_return','map_acquire','map_release','uaccess_clear','initialize')
        for index,name in enumerate(public): self.assertEqual(symbols[name],0x7000+7*index)
        self.assertEqual(symbols['control_abort'],0x705b)
        self.assertEqual(symbols['descriptor_readback'],0x705e)
        self.assertLess(rows[-1][0]+len(rows[-1][1]),0xa000)
        sites,data=control_registry(rows,symbols)
        self.assertEqual(int.from_bytes(data[:2],'little'),len(sites))
        self.assertEqual(len(sites),len(set(pc for pc,_ in sites)))
        expected={pc for pc,_,text in rows if symbols['owned_start']<=pc<symbols['owned_end'] or
                  symbols['finish_publish']<=pc<symbols['abort_impl'] and text=='RET'}
        self.assertEqual({pc for pc,_ in sites},expected)
        for pc,flags in sites:
            if not symbols['owned_start']<=pc<symbols['owned_end']: self.assertEqual(flags,0x1001)
        self.assertLess(0xa000+len(data),0xc000)
        for name in public[:12]:
            self.assertFalse(dict(sites)[symbols['commit_'+name]]&1)
            self.assertTrue(dict(sites)[symbols['committed_'+name]]&1)
        copy_source=(ROOT/'examples/kernel_copy.asm').read_text()
        copy_symbols=image_symbols(copy_source,assemble(copy_source,0x1500),0x1500)
        memory=(ROOT/'examples/kernel_memory.asm').read_text()
        self.assertIn(f'CMPI.W #${copy_symbols["copy_fault"]:04X},r0',memory)
        self.assertIn(f'MOV.W #${copy_symbols["copy_error"]:04X},r0',memory)
        for name in ('boot','irq','swi','memory','copy','load_context'):
            source=(ROOT/f'examples/kernel_{name}.asm').read_text()
            self.assertNotRegex('\n'.join(line.split(';',1)[0] for line in source.splitlines()),r'\bSETC\b',name)

    def test_far_calls(self):
        for target in (0,1,0xffff,0x10000,0x10200,0xffffff):
            source=f'CALLF ${target:X}'
            expected=b'\x54'+target.to_bytes(4,'little')
            self.assertEqual(assemble(source)[0][1],expected)
            formatted,length=disassemble(expected)
            self.assertEqual(length,5)
            self.assertEqual(assemble(formatted)[0][1],expected)
            for n in range(5):
                with self.assertRaisesRegex(ValueError,'Missing|Truncated'): decode(expected[:n])
        self.assertEqual(assemble('RETF')[0][1],b'\x55')
        self.assertEqual(disassemble(b'\x55'),('RETF',1))
        for source in ('CALLF','CALLF r4','CALLF [r4]','CALLF #$010200','CALLF 1,2',
                       'CALLF -1','CALLF $1000000','CALLF.W $10200','RETF r0','RETF.W'):
            with self.subTest(source=source),self.assertRaises(ValueError): assemble(source)
        # A flat label is a numeric offset in segment zero, as with JL.
        listing=assemble('CALLF done\nRETF\ndone: HALT',0x100)
        self.assertEqual(listing[0][1],bytes.fromhex('54 06 01 00 00'))
        self.assertEqual(assemble('CALLF 0',0xfffb)[0][1],bytes.fromhex('54 00 00 00 00'))
        with self.assertRaises(ValueError): assemble('CALLF end\nend:',0xfffb)
        with self.assertRaises(ValueError): assemble('CALLF 0',0xfffc)
        self.assertEqual(assemble('RETF',0xffff)[0][1],b'\x55')
        # Reserved packed bits are a post-fetch CPU Invalid State check,
        # exactly as JL; the decoder must not mistake them for early illegality.
        entry,values=decode(bytes.fromhex('54 00 02 01 FF'))
        self.assertEqual((entry['id'],values['target']),('callf',0xff010200))
        for name,origin,expected in (
            ('far_call.asm',0x100,'15 00 05 00 15 01 01 00 54 00 02 01 00 01'),
            ('far_callee.asm',0x200,'1F 10 05 02 55 10 64 1F 11')):
            rows=assemble((ROOT/'examples'/name).read_text(),origin)
            data=b''.join(row[1] for row in rows)
            self.assertEqual(data,bytes.fromhex(expected))
            for pc,code,_ in rows:
                text,length=disassemble(code,pc)
                self.assertEqual(length,len(code))
                self.assertEqual(assemble(text,pc)[0][1],code)

    def test_immediate_shifts(self):
        for index,op in enumerate(('SHLI','SHRI','SARI','ROLI','RORI')):
            for bits,offset in ((8,0),(16,4)):
                for reg in range(4):
                    tag=0x60+8*index+offset+reg
                    for count in range(17):
                        source=f'{op}.{"B" if bits==8 else "W"} #{count},r{reg}'
                        data=bytes([0x1a,tag,count])
                        self.assertEqual(assemble(source)[0][1],data)
                        text,length=disassemble(data)
                        self.assertEqual((text,length),(source,3))
                        self.assertEqual(assemble(text)[0][1],data)
                        if bits==16:
                            self.assertEqual(assemble(f'{op} #{count},r{reg}')[0][1],data)
                    for n in (0,1,2):
                        with self.assertRaisesRegex(ValueError,'Missing|Truncated'): decode(data[:n])
                    # Counts are semantic state constraints, not decode masks.
                    for count in range(17,256):
                        _,values=decode(bytes([0x1a,tag,count]))
                        self.assertEqual(values['count'],count)
                for source in (f'{op}.B #-1,r0',f'{op}.B #17,r0',f'{op}.W #256,r0',
                               f'{op}.B #65536,r0',f'{op}.W r1,r0',f'{op} #1,r4',
                               f'{op} #1,sp',f'{op}.B #1,[r4]',f'{op}.Q #1,r0',
                               f'{op}',f'{op} #1',f'{op} #1,r0,r1'):
                    with self.subTest(source=source),self.assertRaises(ValueError): assemble(source)
        for tag in (0x4c,0x5f,0x88,0xff):
            with self.assertRaisesRegex(ValueError,'Illegal'): decode(bytes([0x1a,tag]))
        self.assertEqual(assemble('ROLI.W #$10,r3')[0][1],bytes.fromhex('1A 7F 10'))
        listing=assemble('SHRI.W #8,r0\nBEQ end\nRORI.B #16,r1\nend: HALT')
        self.assertEqual(listing[1][1],bytes.fromhex('60 03'))
        self.assertEqual(assemble('SHLI.B #0,r0',0xfffd)[0][1],bytes.fromhex('1A 60 00'))
        with self.assertRaises(ValueError): assemble('SHLI.B #0,r0',0xfffe)
        rows=assemble((ROOT/'examples/immediate_shifts.asm').read_text())
        expected=bytes.fromhex('15 00 AB 12 11 1A 6C 08 15 50 00 20 1A 7D 08 15 51 02 20 '
            '15 00 AB 12 1A 64 10 15 50 04 20 15 00 AB 80 1A 74 10 15 50 06 20 '
            '15 00 AB 12 1A 7C 10 15 50 08 20 1A 84 10 15 50 0A 20 '
            '15 00 82 12 1A 78 08 15 50 0C 20 1A 80 10 15 50 0E 20 '
            '15 00 AB 12 1A 60 00 15 50 10 20 01')
        self.assertEqual(b''.join(code for _,code,_ in rows),expected)
        self.assertEqual((len(rows),len(expected)),(26,89))
        for pc,code,_ in rows:
            text,length=disassemble(code,pc)
            self.assertEqual(length,len(code))
            self.assertEqual(assemble(text,pc)[0][1],code)

    def test_bit_test_encodings(self):
        payload=(ROOT/'doc/ISA_ENCODING.md').read_text()
        with patch('encoding.Path.read_text',return_value=payload.replace('"schema_version": 5','"schema_version": 1')):
            with self.assertRaisesRegex(ValueError,'schema'): metadata()
        for reg in range(4):
            for source in (f'TST r{reg}',f'TST.W r{reg}'):
                self.assertEqual(assemble(source)[0][1],bytes([0x10,0x20+5*reg]))
            unary=bytes([0x1a,0x40+reg])
            self.assertEqual(assemble(f'TST.B r{reg}')[0][1],unary)
            for bits,tag in ((8,0x44),(16,0x48)):
                for value in (-(1<<(bits-1)),-1,0,1,1<<(bits-1),(1<<bits)-1):
                    source=f'TSTI.{"B" if bits==8 else "W"} #{value},r{reg}'
                    data=bytes([0x1a,tag+reg])+(value&((1<<bits)-1)).to_bytes(bits//8,'little')
                    self.assertEqual(assemble(source)[0][1],data)
                    text,length=disassemble(data)
                    self.assertEqual(length,len(data))
                    self.assertEqual(assemble(text)[0][1],data)
                    for n in range(1,len(data)):
                        with self.assertRaisesRegex(ValueError,'Missing|Truncated'): decode(data[:n])
            self.assertEqual(disassemble(unary),(f'TST.B r{reg}',2))
        self.assertEqual(assemble('TSTI #$8000,r1')[0][1],bytes.fromhex('1A 49 00 80'))
        for source in ('TST.Q r0','TSTI.Q #1,r0','TST.B','TST r0,r1','TSTI r0',
                       'TSTI #1,r0,r1','TSTI.B #256,r0','TSTI.B #-129,r0',
                       'TSTI.W #65536,r0','TSTI.W #-32769,r0','TSTI.B 1,r0',
                       'TSTI.B #1,[r4]','TST sp','TST.B r4','TST.W r4','TSTI.W #1,r7'):
            with self.subTest(source=source),self.assertRaises(ValueError): assemble(source)
        for tag in (0x34,0x3f,0x4c,0x4f,0x50,0xff):
            with self.assertRaisesRegex(ValueError,'Illegal'): decode(bytes([0x1a,tag]))
        # Label layout must include the immediate and both preceding escape bytes.
        listing=assemble('TSTI.W #1,r0\nBEQ done\nTST.B r0\ndone: HALT')
        self.assertEqual(listing[1][1],bytes.fromhex('60 02'))
        for source,size in (('TST.B r0',2),('TSTI.B #1,r0',3),('TSTI.W #1,r0',4)):
            self.assertEqual(len(assemble(source,65536-size)[0][1]),size)
            with self.assertRaises(ValueError): assemble(source,65537-size)

    def test_sampled_status_and_costs(self):
        # Compare real assigned encodings without private study opcodes.
        def cost(lines):
            return sum(len(assemble(line)[0][1]) for line in lines),len(lines)
        sampled=['MOV.B [r4],r0','TSTI.B #1,r0','BNE $200','TSTI.B #2,r0','BNE $200']
        copies=['MOV.B [r4],r0','MOV r0,r1','ANDI.B #1,r1','BNE $200','MOV r0,r1','ANDI.B #2,r1','BNE $200']
        reloads=['MOV.B [r4],r0','ANDI.B #1,r0','BNE $200','MOV.B [r4],r0','ANDI.B #2,r0','BNE $200']
        self.assertEqual(cost(sampled),(15,5))
        self.assertEqual(cost(copies),(15,7))
        self.assertEqual(cost(reloads),(16,6))
        # Explicit read-to-consume fixture: successive reads return different
        # samples. This is a study fixture, not a UART register assignment.
        samples=iter([3,0]); reads=[]
        def read_port():
            value=next(samples); reads.append(value); return value
        snapshot=read_port()
        self.assertEqual((bool(snapshot&1),bool(snapshot&2)),(True,True))
        self.assertEqual(reads,[3])
        self.assertFalse(bool(read_port()&2))
        self.assertEqual(reads,[3,0])

    def test_example_sources(self):
        from generate_kernel_fixtures import IMAGES
        for path in sorted((ROOT/'examples').glob('*.asm')):
            name=path.stem.removeprefix('kernel_')
            origin=IMAGES.get(name,0x200 if path.stem in ('far_callee','syscall_handler') else 0x100)
            with self.subTest(source=path.name):
                for pc,data,_ in assemble(path.read_text(),origin=origin):
                    text,length=disassemble(data,pc)
                    self.assertEqual(length,len(data))
                    self.assertEqual(assemble(text,origin=pc)[0][1],data)

    def test_published(self):
        text=(ROOT/'doc/ISA_EXAMPLES.md').read_text().split('<!-- WORKED_IMAGES_END -->')[0]
        count=0
        for block in re.findall(r'```text\n(.*?)```', text,re.S):
            source=[]; expected=bytearray(); origin=None
            for line in block.splitlines():
                m=re.fullmatch(r'([0-9A-F]{4})  ((?:[0-9A-F]{2} ?)+) +(.+)',line)
                if m:
                    if origin is None: origin=int(m[1],16)
                    data=bytes.fromhex(m[2]); expected.extend(data); source.append(m[3]); count+=1
                    e,_=decode(data); self.assertEqual(e['length'],len(data))
                    formatted,length=disassemble(data,int(m[1],16))
                    # Explicit zero long B1 is accepted but canonical assembly is shorter.
                    self.assertEqual(assemble(formatted,origin=int(m[1],16))[0][1],data)
                elif line.strip().endswith(':'): source.append(line.strip())
            if origin is not None:
                actual=b''.join(row[1] for row in assemble('\n'.join(source),origin=origin))
                self.assertEqual(actual,expected)
        self.assertGreater(count,150)

    def test_validation(self):
        for source in ['MOV r4,r5','MOV sp,sp','MOV.B r0,r1','MOV.W #65536,r0','MOV.B #-129,r0',
                       'MOV.W [sp-1],r0','ADDP #128,r4','ADDP #1,r0','ADJSP #$ff','SWI #-1',
                       'SWI #256','SWI.W #1','SWI','SWI r0','JS r0','JS [r4]','CALL r4',
                       'SETX r0,xtrap','MOV.W [r4+32768],r0','MOV.W [sp+65536],r0']:
            with self.subTest(source=source),self.assertRaises(ValueError): assemble(source)
        for r in range(8): self.assertEqual(assemble(f'MOV r{r},r{r}')[0][1],b'\0')
        self.assertEqual(assemble('TRAP #255')[0][1],b'\x26\xff')

if __name__=='__main__': unittest.main()
