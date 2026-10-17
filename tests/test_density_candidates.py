"""Current occurrence accounting, design gates and non-assignment checks."""
import json
from pathlib import Path
import sys
import unittest
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'tools'))
from evaluate_density_candidates import candidate_contracts, cost_models, scan, post_increment_gate, build_report
from measure_density import image
from encoding import metadata
from isa import assemble
BUILD=None
if '--build' in sys.argv:
    n=sys.argv.index('--build');BUILD=Path(sys.argv[n+1]);del sys.argv[n:n+2]


def fixture(source):
    rows=image('compiler:test',source,256,'test')['instructions']
    for r in rows:r.update(retired=7,ram_read_bytes=r['bytes']*7,ram_write_bytes=0)
    return rows


class CandidateTests(unittest.TestCase):
    def test_increment_savings_exclude_optimizer_savings(self):
        s=scan(fixture('PUSH r0\nMOV.W #1,r0\nPOP r1\nADD r0,r1\nMOV r1,r0\nADDI.W #1,r2\nADDI.B #1,r3\n'))
        c=cost_models(s)
        self.assertEqual(c['compiler_unit']['existing_optimizer_bytes_saved'],8)
        self.assertEqual(c['compiler_unit']['existing_optimizer_retirements_saved'],28)
        self.assertEqual(c['compiler_unit']['additional_inc_dec_bytes_saved'],1)
        self.assertEqual(c['compiler_unit']['additional_inc_dec_retirements_saved'],0)
        self.assertEqual(c['word_unit'],{'bytes_saved':1,'retirements_saved':0})
        self.assertEqual(s['byte_unit'][0]['incremental_bytes_saved'],0)
        # Checked carry branches cannot silently disappear into the five-op pattern.
        self.assertNotIn('compiler_unit_expression',scan(fixture('PUSH r0\nMOV.W #1,r0\nPOP r1\nADD r0,r1\nBCS $100\nMOV r1,r0')))

    def test_sign_extension_uses_optimized_existing_baseline(self):
        s=scan(fixture('ANDI.B #255,r0\nCMPI.W #128,r0\nBCC end\nORI.W #$FF00,r0\nend:\nNOP'))
        c=cost_models(s)['sext']
        self.assertEqual(c['branch_to_shift_bytes_saved'],4)
        self.assertEqual(c['additional_sext_bytes_saved'],4)
        self.assertEqual(c['additional_sext_retirements_saved'],7)
        # Existing two-shift alternative agrees with signed byte extension over
        # every original register value; this is math, not proposed CPU execution.
        for value in range(65536):
            shifted=(value<<8)&65535;signed=shifted if shifted<32768 else shifted-65536
            expected=value&255;expected=expected if expected<128 else expected-256
            self.assertEqual(signed>>8,expected)

    def test_post_increment_scope_and_segment_end_priority(self):
        checks=0
        for width in (1,2):
            for base in range(65536):
                self.assertEqual(post_increment_gate(base,width)=='OK',base+width<65536);checks+=1
        self.assertEqual(checks,131072)
        self.assertEqual(post_increment_gate(65535,1,kind='MMIO'),'Invalid State')
        self.assertEqual(post_increment_gate(65534,2),'Invalid State')
        self.assertEqual(post_increment_gate(65534,1),'OK')
        self.assertEqual(post_increment_gate(65533,2),'OK')
        self.assertEqual(post_increment_gate(0,1,kind='MMIO'),'Memory Access')
        self.assertEqual(post_increment_gate(0,2,next_pc=65536),'Memory Access')
        self.assertEqual(post_increment_gate(65535,1,value_register=4),'Illegal Instruction')
        s=scan(fixture('MOV.B [r4],r0\nMOV.B r0,[r5]\nADDP #1,r4\nADDP #1,r5\n'))
        self.assertEqual(s['post_increment_copy'][0]['incremental_bytes_saved'],4)
        self.assertEqual(s['post_increment_copy'][0]['retirements_saved'],14)
        # No ordinary MOVU/MMIO/alias pair becomes an approved RAM replacement.
        s=scan(fixture('MOVU.B [r4],r0\nADDP #1,r4\nMOV.W r4,[r4]\nADDP #2,r4'))
        self.assertEqual(len(s['post_increment']),1)
        self.assertFalse(s['post_increment'][0]['b0_value'])

    def test_current_layout_call_ranges_and_literal_negation(self):
        rows=fixture('CALL $83\nCALL $84\nMOV.W #7,r0\nMOV.W #0,r1\nSUB r0,r1\nMOV r1,r0')
        # First end=102: target 83 => -127 (fits); second end=106 => -130.
        s=scan(rows);self.assertTrue(s['near_call'][0]['rel8_current_layout_fit']);self.assertFalse(s['near_call'][1]['rel8_current_layout_fit'])
        self.assertTrue(s['negate'][0]['literal_operand_before'])
        self.assertEqual(cost_models(s)['relative_call']['retirements_saved'],0)

    def test_dispositions_and_deferred_syntax(self):
        contracts=candidate_contracts();self.assertEqual(len(contracts),15)
        adopted=[c for c in contracts if c['decision'].startswith('assigned')]
        self.assertEqual({c['id'] for c in adopted},{'inc_dec','post_increment','relative_call','sext'})
        for c in adopted:
            for field in ('semantics','compiler','conformance','budget'):self.assertTrue(c[field])
        for c in contracts:self.assertTrue(c['reconsider'])
        for source in ('MOV.W r4,[r4+]','CALL r4','LDC [r4],ENTRY','LDC [r4],PROC','PUSHM #15','NEG r0','BSWAP r0','CLZ r0','CTZ r0','LEA r0,r4'):
            with self.subTest(source=source),self.assertRaises(ValueError):assemble(source,256)
        # Already assigned families must never return to a deferred catch-all.
        for source in ('MUL r1,r0','DIVU r1,r0','MODS r1,r0','INCP r4','TST.W r0','ROLI.W #8,r0'):self.assertTrue(assemble(source,256))

    @unittest.skipUnless(BUILD,'requires rebuilt native observations')
    def test_rebuilt_census_and_encoding_budget(self):
        r=build_report(BUILD);from generate_density_fixtures import cases
        corpus=json.loads((Path(__file__).resolve().parents[1]/'examples/workloads/corpus.json').read_text())
        self.assertEqual(len(r['per_workload']),len(cases())+2+sum(1+bool(c.get('reference')) for c in corpus['cases']))
        budget=r['encoding_budget'];self.assertEqual(budget['actual']['primary'],{'assigned':113,'unassigned':143})
        self.assertEqual(budget['remaining_allocations'],{'primary_values':0,'ext15_tags':0,'ext1a_tags':0})
        by={w['name']:w for w in r['per_workload']}
        self.assertEqual(by['kernel_demo']['models']['pinned_initialization_post_increment']['retirements_saved'],0)
        self.assertEqual(by['kernel_demo']['models']['word_unit']['retirements_saved'],0)
        self.assertEqual(r['compiler_only_sum_across_44_binaries']['negate']['sequences'],32)
        self.assertLess(by['saves_live']['code_bytes'],by['saves_conservative']['code_bytes'])
        self.assertLess(by['sext_shifts']['actual']['totals']['retired'],by['sext_branch']['actual']['totals']['retired'])
        self.assertEqual(sum(w['family']=='compiler' for w in r['per_workload']),47)

if __name__=='__main__':unittest.main()
