"""Exact short-branch bytes, relocation and deterministic relaxation contracts."""
import sys
import unittest
from pathlib import Path
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'tools'))
from isa import assemble,decode,disassemble,BRANCHES

class Branches(unittest.TestCase):
    def test_every_predicate_and_displacement(self):
        self.assertEqual(len(BRANCHES),15)
        for condition,mnemonic in enumerate(BRANCHES):
            for displacement in range(-128,128):
                target=0x400+2+displacement;source=f'{mnemonic}.S ${target:X}'
                data=bytes((0x60+condition,displacement&255))
                self.assertEqual(assemble(source,0x400)[0][1],data)
                e,v=decode(data);self.assertEqual((e['length'],v['displacement']),(2,displacement))
                text,length=disassemble(data,0x400);self.assertTrue(text.startswith(mnemonic+'.S '));self.assertEqual(length,2)
                self.assertEqual(assemble(text,0x400)[0][1],data)
        with self.assertRaisesRegex(ValueError,'Illegal opcode'):decode(bytes((0x6f,)))
        with self.assertRaisesRegex(ValueError,'Truncated'):decode(bytes((0x6e,)))

    def test_numeric_endpoints_and_forced_long_roundtrip(self):
        for disp in (-129,-128,127,128):
            target=0x400+2+disp
            row=assemble(f'BRA ${target:X}',0x400)[0]
            self.assertEqual(len(row[1]),2 if -128<=disp<=127 else 3)
            if not -128<=disp<=127:
                with self.assertRaisesRegex(ValueError,'range'):assemble(f'BRA.S ${target:X}',0x400)
        for mnemonic in BRANCHES:
            for disp in (-128,-1,0,1,127):
                data=bytes((0x40+BRANCHES.index(mnemonic),disp&255,(disp>>8)&255))
                text,_=disassemble(data,0x400);self.assertTrue(text.startswith(mnemonic+'.W '))
                self.assertEqual(assemble(text,0x400)[0][1],data)
            self.assertEqual(len(assemble(mnemonic+'.W $400',0x400)[0][1]),3)
        for origin,disp in ((0x100,32767),(0x8100,-32768)):
            target=origin+3+disp
            self.assertEqual(len(assemble(f'BRA.W ${target:X}',origin)[0][1]),3)
            with self.assertRaisesRegex(ValueError,'Invalid number'):assemble(f'BRA.W #{target}',origin)
        for target in (0x100+3+32768,0x8100+3-32769):
            origin=0x100 if target>0x8000 else 0x8100
            with self.assertRaisesRegex(ValueError,'range'):assemble(f'BRA.W ${target:X}',origin)

    def test_label_limits_and_own_shrink(self):
        for padding,size in ((127,2),(128,3)):
            rows=assemble('BRA end\n'+'NOP\n'*padding+'end: HALT')
            self.assertEqual(len(rows[0][1]),size)
            self.assertEqual(decode(rows[0][1])[1]['displacement'],padding)
        for padding,size in ((126,2),(127,3)):
            rows=assemble('start: '+'NOP\n'*padding+'BRA start')
            self.assertEqual(len(rows[-1][1]),size)
            self.assertEqual(decode(rows[-1][1])[1]['displacement'],-padding-size)

    def test_cascading_shrink_and_deferred_forced_short(self):
        for first in ('BRA','BRA.S'):
            source=first+' end\nBNE end\n'+'NOP\n'*125+'end: HALT'
            rows=assemble(source);self.assertEqual([len(r[1]) for r in rows[:2]],[2,2])
            self.assertEqual(rows[0][1],bytes((0x6e,127)))
            self.assertEqual(rows,assemble(source))
        source='start: BRA end\n'+'NOP\n'*124+'BNE start\nend: HALT'
        rows=assemble(source);self.assertEqual(len(rows[0][1]),2);self.assertEqual(rows[-2][1],bytes((0x61,128)))
        rows=assemble('BRA.W end\nBNE end\n'+'NOP\n'*125+'end: HALT')
        self.assertEqual([len(r[1]) for r in rows[:2]],[3,2])

    def test_fixed_numeric_targets_cannot_oscillate(self):
        # Both branches initially fit rel8, but the first one's shrink moves
        # the second away from fixed $184. It must be pinned long on restart.
        source='BRA end\nBNE $184\nend: HALT'
        rows=assemble(source);self.assertEqual([len(r[1]) for r in rows[:2]],[2,3])
        self.assertEqual(rows[1][1],bytes((0x41,127,0)))
        self.assertEqual(rows,assemble(source))
        with self.assertRaisesRegex(ValueError,'range'):assemble(source.replace('BNE ','BNE.S '))

    def test_final_segment_layout_and_other_fixups(self):
        rows=assemble('BRA end\nend: HALT',0xfffd)
        self.assertEqual(rows[0][1],bytes((0x6e,0)));self.assertEqual(rows[-1][0],0xffff)
        with self.assertRaises(ValueError):assemble('BRA.W end\nend: HALT',0xfffd)
        self.assertEqual(assemble('BRA.S $FFFE',0xfffe)[0][1],bytes((0x6e,0xfe)))
        with self.assertRaises(ValueError):assemble('BRA.S $FFFF',0xffff)
        rows=assemble('BRA end\nCALL end\nJS end\nCALLF end\nend: HALT')
        target=rows[-1][0]
        for _,data,text in rows[1:-1]:
            # A canonical exact roundtrip checks each distinct target layout.
            formatted,_=disassemble(data);self.assertIn(f'${target:X}',formatted)
        for source in ('BRA.B $100','BRA.Q $100','BRA.S','BNE.W $100,r0','BRA missing',
                       'same: NOP\nSAME: BRA same'):
            with self.subTest(source=source),self.assertRaises(ValueError):assemble(source)

if __name__=='__main__':unittest.main()
