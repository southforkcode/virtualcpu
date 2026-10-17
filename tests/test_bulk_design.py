"""Assigned production descriptor/context encoding contracts."""
from pathlib import Path
import sys
import unittest
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'tools'))
from encoding import metadata, matches
from isa import assemble, decode, disassemble


class BulkDesign(unittest.TestCase):
    def test_assigned_escape_budget(self):
        m=metadata()
        self.assertEqual(sum(any(matches(e,b) for e in m['tables']['ext1f']['entries']) for b in range(256)),109)
        self.assertEqual(m['schema_version'],5)
    def test_assigned_descriptor_round_trips_and_truncations(self):
        for store in (False,True):
            for base in range(4):
                source=f'STC DESC,[r{4+base}]' if store else f'LDC [r{4+base}],DESC'
                expected=bytes((0x1f,0xc4+base if store else 0xc0+base))
                self.assertEqual(assemble(source)[0][1],expected)
                self.assertEqual(disassemble(expected),(source,2))
                self.assertEqual(assemble(source.lower())[0][1],expected)
                self.assertEqual(assemble(source,0xfffe)[0][1],expected)
                for n in range(2):
                    with self.assertRaises(ValueError):decode(expected[:n])
                with self.assertRaises(ValueError):assemble(source,0xffff)
        for source in ('LDC.W [r4],DESC','STC.B DESC,[r4]','LDC [r0],DESC','STC DESC,[sp]','LDC [ds:r4],DESC','LDC [r4+1],DESC','LDC [r4+],DESC','LDC [r4],ENTRY','STC CTX,[r4]','LDC #1,DESC','STC [r4],DESC'):
            with self.subTest(source=source),self.assertRaises(ValueError):assemble(source)
    def test_context_round_trips_and_invalid_forms(self):
        for store in (False,True):
            for base in range(4):
                source=f'{"STX" if store else "LDX"} [r{4+base}]'
                expected=bytes((0x1f,(0xcc if store else 0xc8)+base))
                self.assertEqual(assemble(source)[0][1],expected)
                self.assertEqual(disassemble(expected),(source,2))
                self.assertEqual(assemble(source.lower())[0][1],expected)
                for n in range(2):
                    with self.assertRaises(ValueError):decode(expected[:n])
                with self.assertRaises(ValueError):assemble(source,0xffff)
        for source in ('LDX.W [r4]','STX.B [r4]','LDX [r0]','STX [sp]','LDX [ds:r4]','LDX [r4+1]','STX [r4+]','LDX [r4],CTX','STX #1'):
            with self.subTest(source=source),self.assertRaises(ValueError):assemble(source)
        for tag in range(0xd0,0x100):
            with self.assertRaisesRegex(ValueError,'Illegal'):decode(bytes((0x1f,tag)))

    def test_bootable_example_uses_production_encodings(self):
        from generate_bulk_fixtures import IMAGES
        from generate_kernel_fixtures import image_symbols
        seen=set()
        for name,origin in IMAGES.items():
            source=(Path(__file__).resolve().parents[1]/f'examples/bulk_{name}.asm').read_text()
            rows=assemble(source,origin)
            for pc,code,_ in rows:
                text,length=disassemble(code,pc)
                self.assertEqual(assemble(text,pc)[0][1],code)
                self.assertEqual(length,len(code))
                if code[:1]==b'\x1f' and 0xc0<=code[1]<=0xcf:seen.add(code[1]&0xfc)
            if name=='load':self.assertEqual(image_symbols(source,rows,origin)['load_context'],0x700)
        self.assertEqual(seen,{0xc0,0xc4,0xc8,0xcc})

    def test_context_image_cannot_import_frame_authority(self):
        m=metadata()
        self.assertEqual(set(m['context_writable_indices']),set(range(17))|{0x20,0x21,0x22})
        for name in ('xorigin_mode','xreturn_mode','xprior_depth','xframe_depth','xbootstrap',
                     'xcause','xaddrlo','xaddrhi','xtrap','xirq_source','xpriv_class'):
            self.assertNotIn(m['context_indices'][name],m['context_writable_indices'])

if __name__=='__main__':unittest.main()
