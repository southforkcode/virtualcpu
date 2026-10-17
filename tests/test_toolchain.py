import re
import copy
import sys
import unittest
from unittest.mock import patch
from pathlib import Path
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'tools'))
from isa import assemble, decode, disassemble, TABLES, ROOT
from encoding import available, metadata, validate


class Toolchain(unittest.TestCase):
    def test_maps(self):
        # The normative allocation ledger, rather than the former runtime subset,
        # sets the expected union count for every table.
        manual=(ROOT/'doc/ISA.md').read_text()
        labels={'primary':'Working primary map','ext10':'`ext10`','ext15':'`ext15`',
                'ext1a':'`ext1a` (working / metadata union)','ext1f':'Working `ext1f`','ext27':'Working `ext27`'}
        for name,rows in TABLES.items():
            line=next(line for line in manual.splitlines() if line.startswith('| '+labels[name]+' |'))
            count=int(line.split('|')[3].strip().split(',')[0])
            self.assertEqual(sum(e is not None for e in rows),count,name)
        for revision in ('0.4','0.5'):
            self.assertEqual(sum(bool(available(e,b,revision)) for b,e in enumerate(TABLES['ext1a'])),28)
            self.assertEqual(sum(bool(available(e,b,revision)) for b,e in enumerate(TABLES['primary'])),43 if revision=='0.4' else 44)

    def test_metadata_validation(self):
        for mutate in (
            lambda m: m.update(schema_version=2),
            lambda m: m.update(instruction_bytes={'min':1,'max':6}),
            lambda m: m['tables']['primary']['entries'].append(copy.deepcopy(m['tables']['primary']['entries'][0])),
            lambda m: m['tables']['primary']['entries'][0].update(length=6),
            lambda m: m['tables']['primary']['entries'][0].update(match={'mask':0,'value':1}),
            lambda m: m['tables']['ext27']['entries'][0]['fields'][0].update(byte=5),
            lambda m: m['tables']['ext27']['entries'][0]['encoding_checks'][0].update(byte=5),
            lambda m: m['tables']['ext1f']['entries'][-1]['encoding_checks'][0].update(allowed_values=[256]),
        ):
            data=copy.deepcopy(metadata()); mutate(data)
            with self.assertRaises(ValueError): validate(data)

    def test_immediate_alu(self):
        for kind,op in enumerate(('ADDI','SUBI','CMPI','ANDI','ORI','XORI')):
            for bits in (8,16):
                for reg in range(4):
                    for value in (-(1<<(bits-1)),-1,0,1,1<<(bits-1),(1<<bits)-1):
                        source=f'{op}.{"B" if bits==8 else "W"} #{value},r{reg}'
                        data=bytes([0x80+kind*8+(4 if bits==16 else 0)+reg])+(value&((1<<bits)-1)).to_bytes(bits//8,'little')
                        self.assertEqual(assemble(source,'working')[0][1],data)
                        text,length=disassemble(data,revision='working')
                        self.assertEqual(length,len(data)); self.assertEqual(assemble(text,'working')[0][1],data)
                        for n in range(len(data)):
                            with self.assertRaises(ValueError): decode(data[:n],'working')
                        for mode in ('0.4','0.5'):
                            with self.assertRaises(ValueError): assemble(source,mode)
                            with self.assertRaisesRegex(ValueError,'Illegal'): decode(data[:1],mode)
                self.assertEqual(assemble(f'{op} #1,r0','working')[0][1],bytes([0x84+kind*8,1,0]))
                for source in (f'{op}.Q #1,r0',f'{op}.B #256,r0',f'{op}.B #-129,r0',f'{op}.W #65536,r0',
                               f'{op}.W #-32769,r0',f'{op}.B #1,r4',f'{op}.B #1,sp',f'{op}.B r0,r1'):
                    with self.assertRaises(ValueError): assemble(source,'working')

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
                        with self.assertRaises(ValueError): assemble(source,'working')
                        continue
                    if op.endswith('X') and index<0x20:
                        data=bytes([0x1f,(0x20 if op=='GETX' else 0x40)+index,reg])
                    else: data=bytes([0x1f,tags+reg,index])
                    self.assertEqual(assemble(source,'working')[0][1],data)
                    numeric=f'{op} {index},r{reg}' if op.startswith('GET') else f'{op} r{reg},{index}'
                    self.assertEqual(assemble(numeric,'working')[0][1],data)
                    text,length=disassemble(data,revision='working'); self.assertEqual(length,3)
                    self.assertEqual(assemble(text,'working')[0][1],data)
                    for n in range(3):
                        with self.assertRaises(ValueError): decode(data[:n],'working')
                    if not op.endswith('X') or index>=0x20:
                        for mode in ('0.4','0.5'):
                            with self.assertRaises(ValueError): assemble(source,mode)
                            with self.assertRaisesRegex(ValueError,'Illegal'): decode(data[:2],mode)
        for tag,allowed in ((0x80,set(range(0x20,0x2a))|set(range(0x30,0x3b))),
                            (0x90,{0x20,0x21,0x22}),(0xa0,set(m['control_indices'].values())-{127}),
                            (0xb0,set(m['control_writable_indices']))):
            for index in range(256):
                if index in allowed: decode(bytes([0x1f,tag,index]),'working')
                else:
                    with self.assertRaisesRegex(ValueError,'Illegal|Reserved'): decode(bytes([0x1f,tag,index]),'working')
        for source in ('GETC mode,r8','GETC mode,sp','SETC r0,256','GETX 255,r0','SETX r0,xorigin_mode','GETC.W mode,r0'):
            with self.assertRaises(ValueError): assemble(source,'working')

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
                            self.assertEqual(assemble(source,'working')[0][1],data)
                            text,length=disassemble(data,revision='working'); self.assertEqual(length,len(data))
                            self.assertEqual(assemble(text,'working')[0][1],data)
                            for n in range(len(data)):
                                with self.assertRaises(ValueError): decode(data[:n],'working')
                            for mode in ('0.4','0.5'):
                                with self.assertRaises(ValueError): assemble(source,mode)
                                with self.assertRaisesRegex(ValueError,'Illegal'): decode(data[:1],mode)
        for byte in range(256):
            for base in (0,3,4,255):
                raw=bytes([0x27,byte,base,0,0])
                if byte&0x83==byte&0x80 and base<4: decode(raw,'working')
                else:
                    with self.assertRaisesRegex(ValueError,'Illegal|Reserved'): decode(raw,'working')
        text,length=disassemble(bytes.fromhex('27 40 00 00 00'),revision='working')
        self.assertEqual(length,5); self.assertEqual(assemble(text,'working')[0][1],bytes.fromhex('27 00 00'))
        for source in ('MOVU.B [sp],r0','MOVU [ds:r4],r0','MOVU [r0],r0','MOVU #1,r0',
                       'MOVU r0,r1','MOVU [r4],sp','MOVU.Q [r4],r0','MOVU [r4+32768],r0','MOVU [r4-32769],r0'):
            with self.assertRaises(ValueError): assemble(source,'working')

    def test_kernel_images(self):
        from generate_kernel_fixtures import IMAGES, generate
        self.assertIn('namespace kernel_images',generate())
        for name,origin in IMAGES.items():
            rows=assemble((ROOT/f'examples/kernel_{name}.asm').read_text(),'working',origin)
            for pc,code,_ in rows:
                text,length=disassemble(code,pc,'working')
                self.assertEqual(length,len(code)); self.assertEqual(assemble(text,'working',pc)[0][1],code)

    def test_far_calls(self):
        for target in (0,1,0xffff,0x10000,0x10200,0xffffff):
            source=f'CALLF ${target:X}'
            expected=b'\x54'+target.to_bytes(4,'little')
            self.assertEqual(assemble(source,'working')[0][1],expected)
            formatted,length=disassemble(expected,revision='working')
            self.assertEqual(length,5)
            self.assertEqual(assemble(formatted,'working')[0][1],expected)
            for n in range(5):
                with self.assertRaisesRegex(ValueError,'Missing|Truncated'): decode(expected[:n],'working')
        self.assertEqual(assemble('RETF','working')[0][1],b'\x55')
        self.assertEqual(disassemble(b'\x55',revision='working'),('RETF',1))
        for mode in ('0.4','0.5'):
            for source in ('CALLF $010200','RETF'):
                with self.assertRaises(ValueError): assemble(source,mode)
            for primary in (0x54,0x55):
                with self.assertRaisesRegex(ValueError,'Illegal'): decode(bytes([primary]),mode)
        for source in ('CALLF','CALLF r4','CALLF [r4]','CALLF #$010200','CALLF 1,2',
                       'CALLF -1','CALLF $1000000','CALLF.W $10200','RETF r0','RETF.W'):
            with self.subTest(source=source),self.assertRaises(ValueError): assemble(source,'working')
        # A flat label is a numeric offset in segment zero, as with JL.
        listing=assemble('CALLF done\nRETF\ndone: HALT','working',0x100)
        self.assertEqual(listing[0][1],bytes.fromhex('54 06 01 00 00'))
        self.assertEqual(assemble('CALLF 0','working',0xfffb)[0][1],bytes.fromhex('54 00 00 00 00'))
        with self.assertRaises(ValueError): assemble('CALLF end\nend:','working',0xfffb)
        with self.assertRaises(ValueError): assemble('CALLF 0','working',0xfffc)
        self.assertEqual(assemble('RETF','working',0xffff)[0][1],b'\x55')
        # Reserved packed bits are a post-fetch CPU Invalid State check,
        # exactly as JL; the decoder must not mistake them for early illegality.
        entry,values=decode(bytes.fromhex('54 00 02 01 FF'),'working')
        self.assertEqual((entry['id'],values['target']),('callf',0xff010200))
        for name,origin,expected in (
            ('far_call.asm',0x100,'15 00 05 00 15 01 01 00 54 00 02 01 00 01'),
            ('far_callee.asm',0x200,'1F 10 05 02 55 10 64 1F 11')):
            rows=assemble((ROOT/'examples'/name).read_text(),'working',origin)
            data=b''.join(row[1] for row in rows)
            self.assertEqual(data,bytes.fromhex(expected))
            for pc,code,_ in rows:
                text,length=disassemble(code,pc,'working')
                self.assertEqual(length,len(code))
                self.assertEqual(assemble(text,'working',pc)[0][1],code)

    def test_immediate_shifts(self):
        for index,op in enumerate(('SHLI','SHRI','SARI','ROLI','RORI')):
            for bits,offset in ((8,0),(16,4)):
                for reg in range(4):
                    tag=0x60+8*index+offset+reg
                    for count in range(17):
                        source=f'{op}.{"B" if bits==8 else "W"} #{count},r{reg}'
                        data=bytes([0x1a,tag,count])
                        self.assertEqual(assemble(source,'working')[0][1],data)
                        text,length=disassemble(data,revision='working')
                        self.assertEqual((text,length),(source,3))
                        self.assertEqual(assemble(text,'working')[0][1],data)
                        if bits==16:
                            self.assertEqual(assemble(f'{op} #{count},r{reg}','working')[0][1],data)
                    for mode in ('0.4','0.5'):
                        with self.assertRaises(ValueError): assemble(source,mode)
                        with self.assertRaisesRegex(ValueError,'Illegal'): decode(bytes([0x1a,tag]),mode)
                    for n in (0,1,2):
                        with self.assertRaisesRegex(ValueError,'Missing|Truncated'): decode(data[:n],'working')
                    # Counts are semantic state constraints, not decode masks.
                    for count in range(17,256):
                        _,values=decode(bytes([0x1a,tag,count]),'working')
                        self.assertEqual(values['count'],count)
                for source in (f'{op}.B #-1,r0',f'{op}.B #17,r0',f'{op}.W #256,r0',
                               f'{op}.B #65536,r0',f'{op}.W r1,r0',f'{op} #1,r4',
                               f'{op} #1,sp',f'{op}.B #1,[r4]',f'{op}.Q #1,r0',
                               f'{op}',f'{op} #1',f'{op} #1,r0,r1'):
                    with self.subTest(source=source),self.assertRaises(ValueError): assemble(source,'working')
        for tag in (0x4c,0x5f,0x88,0xff):
            with self.assertRaisesRegex(ValueError,'Illegal'): decode(bytes([0x1a,tag]),'working')
        self.assertEqual(assemble('ROLI.W #$10,r3','working')[0][1],bytes.fromhex('1A 7F 10'))
        listing=assemble('SHRI.W #8,r0\nBEQ end\nRORI.B #16,r1\nend: HALT','working')
        self.assertEqual(listing[1][1],bytes.fromhex('40 03 00'))
        self.assertEqual(assemble('SHLI.B #0,r0','working',0xfffd)[0][1],bytes.fromhex('1A 60 00'))
        with self.assertRaises(ValueError): assemble('SHLI.B #0,r0','working',0xfffe)
        rows=assemble((ROOT/'examples/immediate_shifts.asm').read_text(),'working')
        expected=bytes.fromhex('15 00 AB 12 11 1A 6C 08 15 50 00 20 1A 7D 08 15 51 02 20 '
            '15 00 AB 12 1A 64 10 15 50 04 20 15 00 AB 80 1A 74 10 15 50 06 20 '
            '15 00 AB 12 1A 7C 10 15 50 08 20 1A 84 10 15 50 0A 20 '
            '15 00 82 12 1A 78 08 15 50 0C 20 1A 80 10 15 50 0E 20 '
            '15 00 AB 12 1A 60 00 15 50 10 20 01')
        self.assertEqual(b''.join(code for _,code,_ in rows),expected)
        self.assertEqual((len(rows),len(expected)),(26,89))
        for pc,code,_ in rows:
            text,length=disassemble(code,pc,'working')
            self.assertEqual(length,len(code))
            self.assertEqual(assemble(text,'working',pc)[0][1],code)

    def test_bit_test_encodings(self):
        payload=(ROOT/'doc/ISA_ENCODING.md').read_text()
        with patch('encoding.Path.read_text',return_value=payload.replace('"schema_version": 3','"schema_version": 1')):
            with self.assertRaisesRegex(ValueError,'schema'): metadata()
        with patch('encoding.Path.read_text',return_value=payload.replace('"minimum_runtime": "working"','"minimum_runtime": "unknown"')):
            with self.assertRaisesRegex(ValueError,'minimum runtime'): metadata()
        for reg in range(4):
            for source in (f'TST r{reg}',f'TST.W r{reg}'):
                for mode in ('0.4','0.5','working'):
                    self.assertEqual(assemble(source,mode)[0][1],bytes([0x10,0x20+5*reg]))
            unary=bytes([0x1a,0x40+reg])
            self.assertEqual(assemble(f'TST.B r{reg}','working')[0][1],unary)
            for bits,tag in ((8,0x44),(16,0x48)):
                for value in (-(1<<(bits-1)),-1,0,1,1<<(bits-1),(1<<bits)-1):
                    source=f'TSTI.{"B" if bits==8 else "W"} #{value},r{reg}'
                    data=bytes([0x1a,tag+reg])+(value&((1<<bits)-1)).to_bytes(bits//8,'little')
                    self.assertEqual(assemble(source,'working')[0][1],data)
                    text,length=disassemble(data,revision='working')
                    self.assertEqual(length,len(data))
                    self.assertEqual(assemble(text,'working')[0][1],data)
                    for mode in ('0.4','0.5'):
                        with self.assertRaises(ValueError): assemble(source,mode)
                        with self.assertRaisesRegex(ValueError,'Illegal'): decode(data[:2],mode)
                    for n in range(1,len(data)):
                        with self.assertRaisesRegex(ValueError,'Missing|Truncated'): decode(data[:n],'working')
            self.assertEqual(disassemble(unary,revision='working'),(f'TST.B r{reg}',2))
            for mode in ('0.4','0.5'):
                with self.assertRaises(ValueError): assemble(f'TST.B r{reg}',mode)
        self.assertEqual(assemble('TSTI #$8000,r1','working')[0][1],bytes.fromhex('1A 49 00 80'))
        for source in ('TST.Q r0','TSTI.Q #1,r0','TST.B','TST r0,r1','TSTI r0',
                       'TSTI #1,r0,r1','TSTI.B #256,r0','TSTI.B #-129,r0',
                       'TSTI.W #65536,r0','TSTI.W #-32769,r0','TSTI.B 1,r0',
                       'TSTI.B #1,[r4]','TST sp','TST.B r4','TST.W r4','TSTI.W #1,r7'):
            with self.subTest(source=source),self.assertRaises(ValueError): assemble(source,'working')
        for tag in (0x34,0x3f,0x4c,0x4f,0x50,0xff):
            with self.assertRaisesRegex(ValueError,'Illegal'): decode(bytes([0x1a,tag]),'working')
        # Label layout must include the immediate and both preceding escape bytes.
        listing=assemble('TSTI.W #1,r0\nBEQ done\nTST.B r0\ndone: HALT','working')
        self.assertEqual(listing[1][1],bytes.fromhex('40 02 00'))
        for source,size in (('TST.B r0',2),('TSTI.B #1,r0',3),('TSTI.W #1,r0',4)):
            self.assertEqual(len(assemble(source,'working',65536-size)[0][1]),size)
            with self.assertRaises(ValueError): assemble(source,'working',65537-size)

    def test_sampled_status_and_costs(self):
        # Compare real assigned encodings without private study opcodes.
        def cost(lines):
            return sum(len(assemble(line,'working')[0][1]) for line in lines),len(lines)
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
        self.assertEqual(count,184)

    def test_validation(self):
        for source in ['MOV r4,r5','MOV sp,sp','MOV.B r0,r1','MOV.W #65536,r0','MOV.B #-129,r0',
                       'MOV.W [sp-1],r0','ADDP #128,r4','ADDP #1,r0','ADJSP #$ff','SWI #-1',
                       'SWI #256','SWI.W #1','SWI','SWI r0','JS r0','JS [r4]','CALL r4',
                       'SETX r0,xtrap','MOV.W [r4+32768],r0','MOV.W [sp+65536],r0']:
            with self.subTest(source=source),self.assertRaises(ValueError): assemble(source)
        for r in range(8): self.assertEqual(assemble(f'MOV r{r},r{r}')[0][1],b'\0')
        self.assertEqual(assemble('TRAP #255')[0][1],b'\x26\xff')
        for source in ['SWI #1','GETX xtrap,r0']:
            with self.assertRaises(ValueError): assemble(source,'0.4')
        for data in [b'\x26',b'\x1f\x34']:
            with self.assertRaisesRegex(ValueError,'Illegal'): decode(data,'0.4')
        with self.assertRaises(ValueError): assemble('NOP','0.6')

if __name__=='__main__': unittest.main()
