"""Compact SP exact bytes, compatibility, canonicalization and layout."""
import sys
import unittest
from pathlib import Path
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'tools'))
from isa import assemble,decode,disassemble,TABLES

class SPOffsets(unittest.TestCase):
    def test_all_short_offsets_and_registers(self):
        for byte in (False,True):
            for store in (False,True):
                for reg in range(8):
                    tag=0x30|(byte<<3)|(store<<6)|reg
                    for offset in range(256):
                        mem=f'[sp+{offset}]';operands=f'r{reg},{mem}' if store else f'{mem},r{reg}'
                        source=f'MOV.{"B" if byte else "W"} {operands}'
                        data=bytes((0x28,tag,offset))
                        self.assertEqual(assemble(source)[0][1],data)
                        e,v=decode(data);self.assertEqual((e['length'],v['offset']),(3,offset))
                        text,length=disassemble(data);self.assertEqual(length,3)
                        self.assertEqual(assemble(text)[0][1],data)

    def test_fallback_and_noncanonical_long(self):
        for tag in range(256):
            if tag not in range(0x30,0x40) and tag not in range(0x70,0x80):continue
            for offset in (0,1,127,128,254,255,256,65535):
                data=bytes((0x15,tag,offset&255,offset>>8))
                text,length=disassemble(data);self.assertEqual(length,4)
                self.assertEqual(decode(data)[1]['offset'],offset)
                expected=bytes((0x28,tag,offset)) if offset<=255 else data
                self.assertEqual(assemble(text)[0][1],expected)
        self.assertEqual(assemble('MOV.B [sp+$FF],r7')[0][1],bytes((0x28,0x3f,0xff)))
        self.assertEqual(assemble('MOV.W [sp+256],r0')[0][1],bytes((0x15,0x30,0,1)))

    def test_reserved_fields_and_fetch(self):
        self.assertEqual(sum(e is not None for e in TABLES['ext28']),32)
        for tag in range(256):
            valid=tag in range(0x30,0x40) or tag in range(0x70,0x80)
            if valid:
                with self.assertRaisesRegex(ValueError,'Truncated'):decode(bytes((0x28,tag)))
            else:
                with self.assertRaisesRegex(ValueError,'Illegal extension'):decode(bytes((0x28,tag)))
        with self.assertRaisesRegex(ValueError,'Missing extension'):decode(bytes((0x28,)))

    def test_unsigned_assembly_limits(self):
        for source in ('MOV.W [sp-1],r0','MOV.B r7,[sp-32768]','MOV [sp+65536],r0',
                       'MOV.B [sp],sp','MOV.W r8,[sp]','MOV.W [ds:sp],r0',
                       'MOV.W [cs:sp+1],r0','MOV.S [sp],r0'):
            with self.subTest(source=source,target='working'),self.assertRaises(ValueError):assemble(source)

    def test_segment_layout_and_branch_relaxation(self):
        self.assertEqual(len(assemble('MOV.W [sp],r0',0xfffd)[0][1]),3)
        with self.assertRaises(ValueError):assemble('MOV.W [sp],r0',0xfffe)
        self.assertEqual(len(assemble('MOV.W [sp+256],r0',0xfffc)[0][1]),4)
        with self.assertRaises(ValueError):assemble('MOV.W [sp+256],r0',0xfffd)
        source='BRA end\nMOV.W [sp],r0\n'+'NOP\n'*124+'end: HALT'
        rows=assemble(source);self.assertEqual(rows[0][1],bytes((0x6e,127)));self.assertEqual(rows[1][1],bytes((0x28,0x30,0)))
        self.assertEqual(len(assemble(source.replace('BRA ','BRA.W '))[0][1]),3)

    def test_measurement_annotations(self):
        from generate_compiler_fixtures import instructions
        rows=instructions('MOV.W [sp+255],r0\nMOV.B r7,[sp+256]',0x100)
        self.assertEqual([(r['addressing'],r['displacement'],r['bytes']) for r in rows],[('sp',255,3),('sp',256,4)])
        from smallc import compile_source
        a=compile_source('u16 f(u16 x){return x+1;}u16 main(){return f(4);}')
        sp=[i for i in a['instructions'] if i.get('addressing')=='sp']
        self.assertTrue(sp);self.assertTrue(all(i['bytes']==3 and i['identifier'].startswith('mov_sp8_') for i in sp))

if __name__=='__main__':unittest.main()
