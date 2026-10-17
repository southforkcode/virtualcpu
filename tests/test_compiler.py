"""Front-end contracts; CPU execution/differential replay live in C++ fixtures."""
import sys
import unittest
from pathlib import Path
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'tools'))
from smallc import Compiler, Parser, Type, compile_source
from isa import assemble, disassemble

class CompilerTests(unittest.TestCase):
    def test_production_assembler_is_the_only_encoder(self):
        for path in sorted((Path(__file__).resolve().parents[1]/'examples/workloads').glob('*.c')):
            with self.subTest(path=path.name):
                library=path.stem=='kernel_helper'
                a=compile_source(path.read_text(),0xb000 if library else 0x100,library)
                rows=assemble(a['assembly'],a['origin'])
                self.assertEqual(bytes(a['code']),b''.join(b for _,b,_ in rows))
                self.assertEqual(len(rows),len(a['instructions']))
                for pc,b,_ in rows:
                    text,length=disassemble(b,pc);self.assertEqual(length,len(b));self.assertEqual(assemble(text,pc)[0][1],b)

    def test_natural_layout_and_static_initialization(self):
        c=Compiler('struct Pair { u8 tag; u16 word; }; struct Pair pairs[2]={{3,4660},{4,65535}}; u16 main(){return 0;}')
        self.assertEqual(c.structs['struct Pair']['size'],4)
        self.assertEqual(c.structs['struct Pair']['fields']['word'][1],2)
        self.assertEqual(c.data,bytes([3,0,0x34,0x12,4,0,255,255]))
        a=compile_source((Path(__file__).resolve().parents[1]/'examples/workloads/local_struct.c').read_text())
        self.assertTrue(any(i.get('addressing')=='b1' and i.get('displacement')==2 and i['bytes']==3 for i in a['instructions']))
        self.assertFalse(any('addressing' in i for i in a['instructions'] if i['identifier']=='mov_b1_b0'))
        from generate_compiler_fixtures import instructions
        reference=instructions('MOV.W [r4-32],r0',0x100)[0]
        self.assertEqual((reference['addressing'],reference['displacement'],reference['bytes']),('b1',-32,3))
        self.assertNotIn('addressing',instructions('MOV r4,r0',0x100)[0])
        branch=instructions('BRA.S $100',0x100)[0]
        self.assertEqual((branch['branch_distance'],branch['bytes']),(-2,2))
        a=compile_source('u16 main(){u16 i=0;while(i<3){i=i+1;}return i;}')
        self.assertTrue(any(i['bytes']==2 and 'branch_distance' in i for i in a['instructions']))
        c=Compiler('struct Bytes {u8 a;u8 b;u8 c;}; struct Bytes data[2]; u16 main(){return sizeof(struct Bytes);}')
        self.assertEqual(c.structs['struct Bytes']['size'],3);self.assertEqual(c.structs['struct Bytes']['align'],1)

    def test_stack_address_space_is_explicit(self):
        good='u16 get(ss u16 *p){return *p;} u16 main(){u16 x=5;return get(&x);}'
        self.assertIn('MOV ss,r2',compile_source(good)['assembly'])
        for source in (
            'u16 get(u16 *p){return *p;} u16 main(){u16 x=5;return get(&x);}',
            'u16 main(){u16 x=5;u16 *p=&x;return 0;}',
            'u16 main(){u16 x=5;return *(u16 *)&x;}',
            'ss u16 *bad(){u16 x=1;return &x;} u16 main(){return 0;}',
        ):
            with self.subTest(source=source),self.assertRaises(ValueError):compile_source(source)

    def test_checked_index_computation_and_failure_tags(self):
        a=compile_source('u16 main(){u8 *p=(u8 *)0;u16 i=40000;u16 stride=2;p[i*stride]=9;return 0;}')
        self.assertTrue(any(i['kind']=='guard' for i in a['instructions']))
        self.assertTrue(any(i['address_alu'] for i in a['instructions']))
        self.assertIn('sc_failure_3:',a['assembly'])
        self.assertIn('SWI #255',a['assembly'])

    def test_frames_and_call_slots(self):
        a=compile_source('u16 f(u16 a,u16 b){u16 local=1;return a+b+local;}u16 main(){return f(2,3);}')
        self.assertEqual(a['frames'],{'f':2,'main':0})
        self.assertIn('[sp+12]',a['assembly']);self.assertIn('ADJSP #4',a['assembly'])
        self.assertEqual(a['assembly'].count('PUSH r7'),2);self.assertEqual(a['assembly'].count('POP r7'),2)

    def test_reproducible_output_and_case_sensitive_names(self):
        source='u16 F(){return 1;}u16 f(){return 2;}u16 main(){return F()+f();}'
        self.assertEqual(compile_source(source),compile_source(source))
        self.assertEqual(compile_source(source)['frames'],{'F':0,'f':0,'main':0})

    def test_unsupported_syntax_is_rejected(self):
        for source in (
            '#include <stdint.h>\nu16 main(){return 0;}',
            'u32 main(){return 0;}',
            'u16 main(){return 7/2;}',
            'u16 main(){u16 n=2;u16 a[n];return 0;}',
            'u16 main(){u16 **p;return 0;}',
            'u16 main(){u16 x=1;u16 n=2;return x<<n;}',
            'u16 main(){return "text";}',
            'u16 main(){u16 x=1;{u16 x=2;}return x;}',
            'struct X{u16 a;};struct X main(){struct X x={1};return x;}',
            'u16 main(){u8 bytes[4];return read(bytes,bytes);}',
            'u16 read(){return 0;}u16 main(){return read();}',
        ):
            with self.subTest(source=source),self.assertRaises((ValueError,KeyError)):compile_source(source)

    def test_constant_casts_and_signed_shifts(self):
        a=compile_source('u16 x=(u8)256+1;i16 y=(i16)65535>>1;u16 main(){return x+y;}')
        self.assertEqual(a['data'],[1,0,255,255])
        for source in ('u8 *p=42;u16 main(){return 0;}', 'u8 *p=(u8 *)(ss u8 *)1;u16 main(){return 0;}', 'u8 *p=(u8 *)65535+1;u16 main(){return 0;}', 'u8 *p=(u8 *)((u8 *)65535+1);u16 main(){return 0;}'):
            with self.assertRaises(ValueError):compile_source(source)

    def test_declaration_and_object_limits(self):
        for source in ('u16 main(){u8 x[0];return 0;}', 'u8 data[4096];u16 main(){return 0;}', 'u16 main(){u16 x[600];return 0;}'):
            with self.subTest(source=source),self.assertRaises(ValueError):compile_source(source)

if __name__=='__main__':unittest.main()
