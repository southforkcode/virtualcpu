"""Production encoding and relaxation tests for density instructions."""
import sys,unittest
from pathlib import Path
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'tools'))
from isa import assemble,decode,disassemble

class DensityInstructions(unittest.TestCase):
    def test_unit_forms(self):
        for op,tag,immediate in [('INC',0xf0,'ADDI'),('DEC',0xf4,'SUBI')]:
            for r in range(4):
                b=bytes([0x1a,tag+r]);self.assertEqual(assemble(f'{op} r{r}')[0][1],b)
                self.assertEqual(disassemble(b),(f'{op}.W r{r}',2))
                self.assertEqual(assemble(f'{op}.B r{r}')[0][1],assemble(f'{immediate}.B #1,r{r}')[0][1])
                for source in (f'{op}.W r4',f'{op}.B r4',f'{op} sp',f'{op}.Q r0',f'{op} #1,r0',op):
                    with self.assertRaises(ValueError):assemble(source)
                with self.assertRaises(ValueError):decode(b[:1])

    def test_post_forms(self):
        for store in (False,True):
            for bits in (8,16):
                for value in range(4):
                    for base in range(4):
                        source=f'MOV.{"B" if bits==8 else "W"} '+(f'r{value},[r{base+4}+]' if store else f'[r{base+4}+],r{value}')
                        raw=bytes([0x15,0x40|(8 if store else 0)|(4 if bits==8 else 0)|value,base])
                        self.assertEqual(assemble(source)[0][1],raw)
                        text,length=disassemble(raw);self.assertEqual(length,3);self.assertEqual(assemble(text)[0][1],raw)
                        for size in (0,1,2):
                            with self.assertRaises(ValueError):decode(raw[:size])
        for source in ('MOV.B [r4+],r4','MOV.W r4,[r4+]','MOV.W [cs:r4+],r0','MOV.B [ds:r4+],r0','MOVU.W [r4+],r0','MOV.W [r4+1+],r0','MOV.W [sp+],r0'):
            with self.assertRaises(ValueError):assemble(source)
        for tag in range(0x40,0x50):
            for descriptor in range(4,256):
                with self.assertRaises(ValueError):decode(bytes([0x15,tag,descriptor]))

    def test_relative_forms_and_ranges(self):
        for short in (False,True):
            for displacement in ((-128,-1,0,1,127) if short else (-32768,-1,0,1,32767)):
                length=2 if short else 3;pc=0x8000
                raw=bytes([0x56 if short else 0x57])+((displacement&((1<<(8*(length-1)))-1)).to_bytes(length-1,'little'))
                source=f'CALLR.{"S" if short else "W"} {pc+length+displacement}'
                self.assertEqual(assemble(source,pc)[0][1],raw)
                text,n=disassemble(raw,pc);self.assertEqual(assemble(text,pc)[0][1],raw)
                for size in range(length):
                    with self.assertRaises(ValueError):decode(raw[:size])
        for source in ('CALLR.B $100','CALLR r4','CALLR #1','CALLR','CALLR $100,$200','CALLR.S $4000'):
            with self.assertRaises(ValueError):assemble(source)
        for origin in (0x100,0x1200):
            rows=assemble('CALLR target\nNOP\ntarget:\nRET',origin)
            self.assertEqual(rows[0][1],bytes([0x56,1]))
        with self.assertRaises(ValueError):assemble('CALLR.W 65535',0)
        with self.assertRaises(ValueError):assemble('CALLR 65535',0) # no silent absolute/PIC fallback

    def test_joint_branch_call_relaxation(self):
        # Own forward shrink, mutually enabling branch/call shortening, forced
        # widths, numeric-target re-expansion and two assembly origins.
        for instruction in ('CALLR','BRA'):
            for gap in (126,127,128):
                rows=assemble(instruction+' target\n'+'NOP\n'*gap+'target:\nRET')
                self.assertEqual(len(rows[0][1]),2 if gap<=127 else 3)
        source='CALLR finish\nBRA finish\n'+'NOP\n'*124+'finish:\nRET'
        rows=assemble(source);self.assertEqual([len(r[1]) for r in rows[:2]],[2,2])
        rows=assemble(source.replace('CALLR ','CALLR.W '));self.assertEqual(len(rows[0][1]),3)
        for source in ('BRA end\nCALLR $184\nend:\nRET','CALLR end\nBRA $184\nend:\nRET'):
            rows=assemble(source);self.assertEqual([len(r[1]) for r in rows],[2,3,2])
            self.assertEqual(decode(rows[1][1])[1]['displacement'],127)
        for source in ('CALLR.S target\n'+'NOP\n'*128+'target:\nRET','BRA $184\nCALLR $80\nRET'):
            if source.startswith('CALLR.S'):
                with self.assertRaises(ValueError):assemble(source)
            else:
                rows=assemble(source)
                for pc,b,_ in rows:self.assertEqual(assemble(disassemble(b,pc)[0],pc)[0][1],b)

    def test_sext(self):
        for r in range(4):
            raw=bytes([0x1a,0xf8+r]);self.assertEqual(assemble(f'SEXT r{r}')[0][1],raw)
            self.assertEqual(assemble(f'SEXT.W r{r}')[0][1],raw);self.assertEqual(disassemble(raw),(f'SEXT r{r}',2))
        for source in ('SEXT.B r0','SEXT r4','SEXT sp','SEXT #1,r0','SEXT'):
            with self.assertRaises(ValueError):assemble(source)

if __name__=='__main__':unittest.main()
