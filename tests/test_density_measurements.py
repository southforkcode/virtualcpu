#!/usr/bin/env python3
"""Accounting and provenance checks for current workload measurements."""
import json
from pathlib import Path
import sys
import tempfile
import unittest
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'tools'))
from measure_density import image, census
from generate_density_fixtures import cases
BUILD=None
if '--build' in sys.argv:
    index=sys.argv.index('--build');BUILD=Path(sys.argv[index+1]);del sys.argv[index:index+2]

class DensityMeasurements(unittest.TestCase):
    def test_categories_and_complete_context_pairs(self):
        im=image('probe:fixture','GETX xr0,r0\nMOV.W r0,[r4]\nMOV.W [r5],r0\nSETX r0,xr0\nSUBI.W #1,r0\nGETC desc_frame,r0\n',256,'fixture')
        self.assertEqual(sum('context_spill_pair' in r['tags'] for r in im['instructions']),2)
        self.assertEqual(sum('context_reload_pair' in r['tags'] for r in im['instructions']),2)
        self.assertIn('unit_arithmetic',im['instructions'][4]['tags'])
        self.assertIn('descriptor_control',im['instructions'][5]['tags'])
    def test_pc_qualification_and_no_silent_unattributed_retirement(self):
        im=image('fixture','NOP\nHALT',256,'fixture')
        raw={'totals':{'retired':2},'sites':{'0:7:1:256':{'retired':1},'0:8:1:256':{'retired':1}}}
        r=census('fixture',[(0,7,1,im),(0,8,1,im)],raw,4,2)
        self.assertEqual(r['distinct_code_bytes'],2)
        self.assertEqual(r['loaded_code_bytes'],4)
        self.assertEqual(r['sites'][0]['retired'],2)
        raw['sites']['0:9:1:256']={'retired':1};raw['totals']['retired']=3
        with self.assertRaisesRegex(ValueError,'unattributed'):census('fixture',[(0,7,1,im),(0,8,1,im)],raw,4,2)
    def test_required_software_contracts_present(self):
        probes={p['name']:p for p in cases()}
        for name in ('descriptor_lifecycle','mul_signed','udiv_zero','sdiv_4','parse_overflow','copy_B_unit','copy_W_unit','call_relative_position_4608','call_indirect_256'):
            self.assertIn(name,probes)
        self.assertEqual(probes['sdiv_4']['inputs'],{'a':-32768,'b':-1,'signed':True})
        self.assertEqual(probes['sdiv_4']['checks'][-1],(0x4002,[0,0]))
    @unittest.skipUnless(BUILD,'requires native execution fixtures')
    def test_full_report_accounting_and_matched_alternatives(self):
        from measure_density import report
        r=report(BUILD);rows={w['name']:w for w in r['workloads']}
        corpus=json.loads((Path(__file__).resolve().parents[1]/'examples/workloads/corpus.json').read_text())
        self.assertEqual(len(rows),len(cases())+2+sum(1+bool(c.get('reference')) for c in corpus['cases']))
        kernel=rows['kernel_demo'];self.assertEqual(kernel['metrics']['maximum_exception_depth'],2)
        self.assertEqual(kernel['metrics']['exception_entries']['2'],2)
        # Four complete spills; incoming context also includes bootstrap load.
        self.assertEqual(sum(s['retired'] for s in kernel['sites'] if s['image']=='irq' and 'context_spill_pair' in s['tags']),0)
        self.assertEqual(kernel['categories']['context_image_store']['retired'],4)
        self.assertEqual(kernel['categories']['context_image_load']['retired'],4)
        self.assertEqual(kernel['categories']['context_reload_pair']['retired'],40)
        for w in rows.values():
            self.assertEqual(sum(s['retired'] for s in w['sites']),w['metrics']['totals']['retired'])
            self.assertGreaterEqual(w['metrics']['totals']['ram_read_bytes'],w['metrics']['totals']['fetch_bytes'])
        self.assertEqual(rows['descriptor_lifecycle']['categories']['descriptor_image_install']['retired'],2)
        self.assertEqual(rows['descriptor_lifecycle']['categories']['descriptor_image_readback']['retired'],3)
        scalar=rows['descriptor_cycle_scalar'];bulk=rows['descriptor_cycle_bulk']
        self.assertLess(bulk['metrics']['totals']['retired'],scalar['metrics']['totals']['retired'])
        self.assertEqual(bulk['categories']['descriptor_image_install']['retired'],2)
        self.assertEqual(bulk['categories']['descriptor_image_readback']['retired'],2)
        self.assertEqual(bulk['categories']['descriptor_remove_body']['retired'],scalar['categories']['descriptor_remove_body']['retired'])
        baseline=rows['scale10_multiply'];alternative=rows['scale10_strength']
        self.assertLess(alternative['metrics']['totals']['retired'],baseline['metrics']['totals']['retired'])
        for name in ('mul_signed','udiv_small_simple','udiv_small_binary','sdiv_0','parse_max','format_65535'):
            native=rows[name+'_native'];software=rows[name]
            self.assertLess(native['distinct_code_bytes'],software['distinct_code_bytes'])
            self.assertLess(native['metrics']['totals']['retired'],software['metrics']['totals']['retired'])
        self.assertNotIn('multiply_loop',rows['mul_signed_native']['categories'])
        self.assertLess(rows['scale8_shift']['distinct_code_bytes'],rows['scale8_native']['distinct_code_bytes'])
        self.assertGreater(rows['variable_stride']['categories']['multiply_loop']['retired'],0)
        self.assertGreater(rows['arithmetic_decimal']['categories']['native_integer_arithmetic']['retired'],0)
        self.assertTrue(rows['fail_divu_preempt']['expected_arithmetic_fault'])
        # Compare actual matched current-ISA probes without archived reports.
        for suffix in ('B','W'):
            unit,dec,post=(rows[f'copy_{suffix}_{kind}'] for kind in ('unit','dec','post'))
            self.assertEqual(dec['distinct_code_bytes'],unit['distinct_code_bytes']-1)
            self.assertEqual(dec['metrics']['totals']['retired'],unit['metrics']['totals']['retired'])
            self.assertEqual(post['metrics']['totals']['retired'],unit['metrics']['totals']['retired']-32)

        from measure_workloads import build_report
        compiler=build_report(BUILD,BUILD/'compiler/execution.json')
        self.assertEqual(compiler['aggregates']['successful_compiled']['count'],27)
        # Evidence output refuses replacement before running any subprocess.
        import subprocess
        with tempfile.TemporaryDirectory() as d:
            p=Path(d)/'report.json';p.write_text('preserve this evidence')
            cli=Path(__file__).resolve().parents[1]/'tools/measure_density.py'
            run=subprocess.run([sys.executable,str(cli),'--build',str(BUILD),'--output',str(p)],capture_output=True,text=True)
            self.assertNotEqual(run.returncode,0);self.assertIn('Refusing to overwrite',run.stderr)
            self.assertEqual(p.read_text(),'preserve this evidence')

if __name__=='__main__':unittest.main()
