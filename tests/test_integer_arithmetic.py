"""All assigned MUL/DIV/MOD streams and the rejected descriptor alternative."""
import copy
from pathlib import Path
import sys
import unittest
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'tools'))
from encoding import metadata,validate,matches
from isa import assemble,decode,disassemble

class IntegerArithmetic(unittest.TestCase):
    def test_all_forms(self):
        for i,op in enumerate(('MUL','DIVU','DIVS','MODU','MODS')):
            for src in range(4):
                for dst in range(4):
                    source=f'{op} r{src},r{dst}'
                    b=bytes((0x1a,0xa0+16*i+4*src+dst))
                    self.assertEqual(assemble(source)[0][1],b)
                    self.assertEqual(assemble(source.lower())[0][1],b)
                    self.assertEqual(assemble(source.replace(op,op+'.W'))[0][1],b)
                    self.assertEqual(disassemble(b),(source,2))
                    self.assertEqual(decode(b)[0]['flags_written'],['N','Z'])
                    for size in (0,1):
                        with self.assertRaises(ValueError):decode(b[:size])
                    self.assertEqual(assemble(source,0xfffe)[0][1],b)
                    with self.assertRaises(ValueError):assemble(source,0xffff)
    def test_bad_operands_and_neighbors(self):
        for op in ('MUL','DIVU','DIVS','MODU','MODS'):
            for tail in ('',' r0',' r0,r1,r2',' r4,r0',' r0,r7',' sp,r0',' #2,r0',' [r4],r0','.B r0,r1','.Q r0,r1'):
                with self.subTest(source=op+tail),self.assertRaises(ValueError):assemble(op+tail)
        for op in ('MULS','MULU','MULU32','MULS32','DIVMODU','DIVMODS'):
            with self.assertRaises(ValueError):assemble(op+' r1,r0')
        for tag in (*range(0x88,0xa0),*range(0xfc,0x100)):
            with self.assertRaisesRegex(ValueError,'Illegal'):decode(bytes((0x1a,tag)))
    def test_capacity_and_three_byte_alternative(self):
        m=metadata();t=m['tables']['ext1a']['entries']
        self.assertEqual(sum(any(matches(e,b) for e in t) for b in range(256)),172)
        candidate=copy.deepcopy(m)
        # A three-byte design could spend five tags and a checked 0000ssdd
        # descriptor. Study only: do not reinterpret assigned bytes.
        t=candidate['tables']['ext1a']['entries']
        t[:]=[e for e in t if not e['semantic'].startswith('integer_')]
        for i,op in enumerate(('MUL','DIVU','DIVS','MODU','MODS')):
            t.append(dict(id='design_'+op,mnemonic=op,match={'mask':255,'value':0x90+i},length=3,
                          fields=[dict(name='source',byte=2,lsb=2,width=2,bank='B0'),dict(name='destination',byte=2,lsb=0,width=2,bank='B0')],
                          operands=[],flags_written=['N','Z'],semantic='design_integer',encoding_checks=[dict(byte=2,mask=240,equals=0)]))
        validate(candidate)
        self.assertEqual(sum(any(matches(e,b) for e in t) for b in range(256)),97)
        self.assertEqual(m['tables']['primary'],candidate['tables']['primary'])

if __name__=='__main__':unittest.main()
