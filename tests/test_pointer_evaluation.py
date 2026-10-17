"""Unencoded pointer model checks; these are not CPU instruction conformance."""
import sys
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT/'tools'))
from evaluate_pointer_arithmetic import checked_add, pointer_sites, site_model, guarded_site_model
from isa import assemble, decode, TABLES
from smallc import compile_source


class PointerEvaluation(unittest.TestCase):
    def test_exhaustive_unsigned_addends_against_carry_baseline(self):
        # 12 bases x every addend, including carries across byte/sign boundaries.
        for base in (0, 1, 127, 128, 255, 256, 32767, 32768, 65280, 65533, 65534, 65535):
            for addend in range(65536):
                wrapped = (base+addend) & 65535
                carry = addend > 65535-base
                if carry:
                    with self.assertRaises(OverflowError):
                        checked_add(base, addend)
                else:
                    self.assertEqual(checked_add(base, addend), wrapped)

    def test_unsigned_is_not_signed_displacement_or_subtraction(self):
        self.assertEqual(checked_add(0, 0xffff), 0xffff)
        self.assertEqual(checked_add(1, 0x8000), 0x8001)
        with self.assertRaises(OverflowError):
            checked_add(1, 0xffff)  # A signed -1 operation would have succeeded.
        for values in ((0, -1), (-1, 0), (0, 65536), (65536, 0)):
            with self.assertRaises(ValueError):
                checked_add(*values)

    def test_scaling_bounds_and_access_width_remain_separate(self):
        # Existing safe lowering checks these before/after the final pointer add.
        outcomes = set()
        for base in (0, 1, 8192, 32767, 65530, 65534, 65535):
            for size in (1, 2, 3, 4, 6):
                for index in (-1, 0, 1, 2, 31, 32, 10922, 21845, 32767, 32768, 65535):
                    for bound in (None, 32):
                        if index < 0:
                            outcomes.add('negative');continue
                        if bound is not None and index >= bound:
                            outcomes.add('bounds');continue
                        if index > 65535//size:
                            outcomes.add('scale');continue
                        offset = index*size
                        safe_result = (base+offset) & 65535
                        if offset > 65535-base:
                            with self.assertRaises(OverflowError):checked_add(base, offset)
                            outcomes.add('pointer');continue
                        candidate = checked_add(base, offset)
                        self.assertEqual(candidate, safe_result)
                        valid_width = candidate <= 65536-size
                        self.assertEqual(valid_width, safe_result+size <= 65536)
                        outcomes.add('success' if valid_width else 'width')
        self.assertEqual(outcomes, {'negative', 'bounds', 'scale', 'pointer', 'width', 'success'})
        self.assertEqual(checked_add(65535, 0), 65535)  # Legal pointer, illegal word access.

    def test_only_pointer_range_guards_are_candidates(self):
        a = compile_source((ROOT/'examples/workloads/variable_stride.c').read_text())
        sites = pointer_sites(a['instructions'])
        self.assertEqual(len(sites), 1)  # Not either checked integer/multiply ADD.
        self.assertEqual(sites[0]['kind'], 'b1_byte_access')
        a = compile_source((ROOT/'examples/workloads/fail_sub.c').read_text())
        self.assertEqual([s['operation'] for s in pointer_sites(a['instructions'])], ['subtract'])
        a = compile_source((ROOT/'examples/workloads/local_array.c').read_text())
        self.assertEqual([s['kind'] for s in pointer_sites(a['instructions'])], ['b1_byte_access'])

    def test_b0_result_and_width_copy_costs_are_not_omitted(self):
        expected = {'array_byte': ('b1_byte_access', 2, 1, 0),
                    'array_word': ('b1_width_checked_access', 0, 0, 1),
                    'pointer_math': ('b0_result', -1, -1, 2)}
        for name, (kind, saved_bytes, saved_retired, added_moves) in expected.items():
            with self.subTest(name=name):
                a = compile_source((ROOT/f'examples/workloads/{name}.c').read_text())
                site = next(s for s in pointer_sites(a['instructions']) if s['operation'] == 'add')
                self.assertEqual(site['kind'], kind)
                model = site_model(site, 2)
                self.assertEqual((model['saved_bytes'], model['saved_retired_instructions'], model['added_bank_moves']),
                                 (saved_bytes, saved_retired, added_moves))
                self.assertEqual(site_model(site, 3)['saved_bytes'], saved_bytes-1)
                self.assertEqual((model['saved_spills'], model['saved_reloads']), (0, 0))
                guarded = guarded_site_model(site, 2)
                self.assertEqual((guarded['added_bytes'], guarded['added_retired_instructions']),
                                 (5, 3) if kind == 'b0_result' else (3, 2))

    def test_current_encodings_and_operand_classes_remain(self):
        for source in ('ADDP.R r0,r4', 'ADDP r0,r4', 'ADD r0,r4',
                       'ADDP.R r0,dp', 'ADDP.R r4,r0'):
            with self.subTest( source=source), self.assertRaises(ValueError):
                assemble(source)
        for reg in range(5):
            target = f'r{4+reg}' if reg < 4 else 'dp'
            self.assertEqual(assemble(f'ADDP #-1,{target}')[0][1], bytes((0x20+reg, 255)))
        for src in range(4):
            for dst in range(4):
                data = assemble(f'ADD r{src},r{dst}')[0][1]
                self.assertEqual((decode(data)[0]['semantic'], len(data)), ('add', 2))
        self.assertEqual(sum(e is not None for e in TABLES['primary']), 111)
        self.assertEqual(sum(e is None for e in TABLES['ext1a']), 176)


if __name__ == '__main__':
    unittest.main()
