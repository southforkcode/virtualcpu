import re
import sys
import unittest
from pathlib import Path
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'tools'))
from isa import assemble, decode, disassemble, TABLES, ROOT


class Toolchain(unittest.TestCase):
    def test_maps(self):
        self.assertEqual([sum(e is not None for e in rows) for rows in TABLES.values()], [44,224,240,28,61])

    def test_published(self):
        text=(ROOT/'doc/ISA_Examples.md').read_text().split('<!-- WORKED_IMAGES_END -->')[0]
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
