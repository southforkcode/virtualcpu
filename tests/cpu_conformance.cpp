#include "cpu.hpp"
#include <fstream>
#include <iostream>
#include <random>
#include <regex>
#include <sstream>
#include <stdexcept>
#include <string>
using namespace vcpu;
static size_t checks=0;
void check(bool ok, const std::string& label) { ++checks; if (!ok) throw std::runtime_error(label); }
void vector_to(VirtualCPU& c, unsigned v, uint32_t target) {
    c.load(4*v,{uint8_t(target),uint8_t(target>>8),uint8_t(target>>16),0});
}
void control(VirtualCPU& c,uint8_t selector,uint16_t value) {
    auto saved=c.registers(); c.load(0x700,{0x1f,0xb7,selector}); c.registers().pc=0x700; c.registers().cs=0;
    c.registers().r[7]=value; c.step(); c.registers()=saved;
}
void irq(VirtualCPU& c) {
    auto saved=c.registers();
    if(c.state()!=ExecutionState::Halted) {
        c.load(0x700,{0x15,0x50,2,0,0x15,0x50,6,0});
        c.registers().cs=0; c.registers().pc=0x700; c.registers().ds=255; c.registers().r[0]=2;
        c.step(); c.step(); c.registers()=saved;
    }
    BoundaryEvent event{c.run(),c.boundary(),c.next_sequence(),BoundaryEvent::Kind::Edge}; event.source=1;
    check(c.apply_boundary_events({event})==HostError::None,"fixture source-specific edge");
}
VirtualCPU machine() {
    VirtualCPU c(0x40000);
    for (unsigned i=0;i<=7;++i) vector_to(c,i,i?0x300:0x100);
    c.load(0x300,{0x1f,0x12}); c.reset();
    control(c,0x10,0); control(c,0x11,0); control(c,0x12,0); control(c,0x13,0xfffe); control(c,0x14,0xfffe); control(c,0x7f,5);
    auto saved=c.registers();c.load(0x700,{0x15,0x50,2,0});
    c.registers().pc=0x700;c.registers().ds=255;c.registers().r[0]=2;c.step();c.registers()=saved;
    return c;
}
void code(VirtualCPU& c, std::initializer_list<uint8_t> b, uint16_t pc=0x100) {
    c.load(pc,std::vector<uint8_t>(b)); c.registers().pc=pc;
}
void faulted(const VirtualCPU& c, Cause cause, uint16_t pc=0x100) {
    check(c.context().has_value(),"missing context");
    check((*c.context())[17]==unsigned(cause),"wrong cause");
    check((*c.context())[8]==pc,"imprecise fault PC");
}
void documented_tests() {
    for (unsigned reg=0;reg<4;++reg) {
        auto c=machine(); code(c,{0x1a,uint8_t(0x40+reg)});
        c.map(0x102,1,false); c.registers().r[reg]=0x12ff; c.registers().flags=0x2d;
        c.step();
        check(!c.context() && c.registers().pc==0x102 && c.registers().r[reg]==0x12ff && c.csr()==0x2e,
              "TST.B complete fetch, preserved register/CVI, byte NZ");
    }
}
uint16_t word(const VirtualCPU& c,uint32_t a) { return c.peek(a)|uint16_t(c.peek(a+1))<<8; }
void arithmetic() {
    auto c=machine(); std::mt19937 rng(16);
    for (int op : {0x64,0x44,0x74,0x54,0x104}) {
        for (unsigned i=0;i<5000;++i) for (int carry=0;carry<2;++carry) {
            code(c,{uint8_t(op==0x104?0x1a:0x10),uint8_t(op)});
            auto& s=c.registers();
            uint16_t a=uint16_t(rng()),b=uint16_t(rng());
            if (i<49) { const uint16_t edge[]={0,1,7,0x7fff,0x8000,0xfffe,0xffff}; a=edge[i/7]; b=edge[i%7]; }
            s.r[0]=a; s.r[1]=b; s.flags=uint16_t(0x28|(carry?4:0));
            bool sub=op==0x74 || op==0x54 || op==0x104;
            int extra=op==0x54?1-carry:op==0x44?carry:0;
            int wide=sub?int(a)-b-extra:int(a)+b+extra;
            auto sx=[](uint16_t x) { return x<32768?int(x):int(x)-65536; };
            int sign=sub?sx(a)-sx(b)-extra:sx(a)+sx(b)+extra;
            uint16_t out=uint16_t(wide);
            unsigned flags=0x20|(out==0?1:0)|(out&0x8000?2:0)|
                ((sub?int(a)>=int(b)+extra:wide>65535)?4:0)|((sign < -32768 || sign>32767)?8:0);
            c.step();
            check(s.r[0]==(op==0x104?a:out) && s.flags==flags,"ALU signed/unsigned oracle");
        }
    }
    for (int f=0;f<16;++f) for (int condition=0;condition<15;++condition) {
        code(c,{uint8_t(0x40+condition),4,0}); c.registers().flags=f;
        bool z=f&1,n=f&2,cy=f&4,v=f&8;
        bool truth[]={z,!z,cy,!cy,n,!n,v,!v,cy&&!z,!cy||z,n==v,n!=v,!z&&n==v,z||n!=v,true};
        c.step(); check(c.registers().pc==(truth[condition]?0x107:0x103),"branch predicate");
    }
    for (auto item : {std::pair<int,int>{0x10,2},{0x20,0x4000},{0x30,0xc000}}) {
        code(c,{0x1a,uint8_t(item.first)}); c.registers().r[0]=0x8001; c.registers().flags=0x28;
        c.step(); check(c.registers().r[0]==item.second && c.csr()==0x2c+(item.first==0x30?2:0),"shift preservation");
    }
}
void bit_tests() {
    auto c=machine();
    auto nz_test=[](uint16_t result,unsigned bits) {
        return unsigned(result==0?1:0)|unsigned(result&(1u<<(bits-1))?2:0);
    };
    // Exhaustive low-byte value/mask pairs and every original upper byte for
    // unary byte tests. No register, pointer, memory or C/V/I side effect.
    for (unsigned a=0;a<65536;++a) {
        code(c,{0x1a,0x40}); c.registers().r[0]=uint16_t(a); c.registers().flags=0x2f;
        c.step();
        check(c.registers().r[0]==a && c.csr()==(0x2c|nz_test(a&255,8)),"TST.B exhaustive register preservation/width");
        unsigned value=a>>8, mask=a&255;
        code(c,{0x1a,0x44,uint8_t(mask)}); c.registers().r[0]=uint16_t(0xa500|value); c.registers().flags=0x2f;
        c.step();
        check(c.registers().r[0]==(0xa500|value) && c.csr()==(0x2c|nz_test(value&mask,8)),"TSTI.B exhaustive value/mask");
    }
    for (unsigned reg=0;reg<4;++reg) for (unsigned tag : {0x40,0x44,0x48})
        for (unsigned a : {0u,1u,0x7fu,0x80u,0xffu,0x100u,0x12ffu,0x8000u,0xff00u,0xffffu})
            for (unsigned mask : {0u,1u,0x80u,0xffu,0x8000u,0xffffu}) for (unsigned f=0;f<32;++f) {
                unsigned bits=tag==0x48?16:8;
                uint16_t used_mask=tag==0x40?255:mask&((1u<<bits)-1);
                code(c,{0x1a,uint8_t(tag+reg),uint8_t(mask),uint8_t(mask>>8)});
                auto& s=c.registers(); s.r.fill(0x2345); s.r[reg]=uint16_t(a);
                s.flags=(f&15)|((f&16)?32:0); s.ds=255; // Uninstalled DS must not be read.
                auto old=s; c.step();
                check(s.r==old.r && s.ds==old.ds && s.ss==old.ss && s.sp==old.sp && s.dp==old.dp,
                      "TST/TSTI snapshot leaves registers unchanged");
                check(s.flags==((old.flags&~3)|nz_test(a&used_mask,bits)) &&
                      s.pc==0x100+(tag==0x40?2:tag==0x44?3:4) && !c.context(),"test flags/length oracle");
            }
    // E is synthesized, preserved in an active context; word TST is OR self.
    for (unsigned reg=0;reg<4;++reg) for (unsigned tag : {0x40,0x44,0x48}) {
        c=machine(); code(c,{0x26,1}); c.step();
        c.load(0x300,{0x1a,uint8_t(tag+reg),0x80,0x80});
        auto saved=*c.context(); c.registers().r[reg]=0xff80; c.registers().flags=12; c.step();
        check(c.context() && *c.context()==saved && c.csr()==0x4e && c.registers().r[reg]==0xff80,"test preserves active frame/E/C/V");
        c=machine(); code(c,{0x10,uint8_t(0x20+5*reg)}); c.registers().r[reg]=0xff00;
        c.step(); check(c.csr()==2 && c.registers().r[reg]==0xff00,"TST.W alias word sign");
    }
    // Every branch consumes fresh N/Z but retained C/V, including signed
    // predicates whose V is deliberately stale rather than a compare result.
    for (unsigned f=0;f<16;++f) for (unsigned condition=0;condition<15;++condition) {
        c=machine(); code(c,{0x1a,0x44,uint8_t(f&1?0:0x80),uint8_t(0x40+condition),1,0});
        c.registers().r[0]=0x1280; c.registers().flags=f; c.step();
        unsigned flags=(f&12)|(f&1?1:2);
        check(c.csr()==flags,"test fresh NZ/retained CV before branch");
        bool z=flags&1,n=flags&2,cy=flags&4,v=flags&8;
        bool truth[]={z,!z,cy,!cy,n,!n,v,!v,cy&&!z,!cy||z,n==v,n!=v,!z&&n==v,z||n!=v,true};
        c.step(); check(c.registers().pc==(truth[condition]?0x107:0x106),"branch after test");
    }
    for (unsigned tag=0;tag<256;++tag) {
        c=machine(); code(c,{0x1a,uint8_t(tag)},0xfffe); c.step();
        bool assigned=tag<0x14 || (tag>=0x20 && tag<=0x23) || (tag>=0x30 && tag<=0x33) || (tag>=0x40 && tag<=0x4b) || (tag>=0x60 && tag<=0x87);
        check(c.context() && (((*c.context())[17]!=1)==assigned),"working ext1a complete map");
    }
    for (unsigned tag=0x40;tag<=0x4b;++tag) {
        unsigned length=tag<0x44?2:tag<0x48?3:4;
        for (unsigned missing=1;missing<length;++missing) {
            c=machine(); code(c,{0x1a,uint8_t(tag),0xff,0xff});
            c.map(0x100+missing,1,false); c.registers().r.fill(0xff00); c.registers().flags=0x2e;
            auto old=c.registers(); c.step(); faulted(c,Cause::Memory);
            check(c.registers().r==old.r && (*c.context())[10]==old.flags,"test missing operand atomicity");
        }
        c=machine();
        code(c,{0x1a,uint8_t(tag),0xff,0xff},0x100); // Place exactly length bytes at segment end.
        std::vector<uint8_t> bytes{0x1a,uint8_t(tag),0xff,0xff}; bytes.resize(length);
        c.load(65536-length,bytes); c.registers().pc=uint16_t(65536-length);
        c.registers().r.fill(0x12ff); c.registers().flags=0x2d;
        auto old=c.registers(); c.step(); faulted(c,Cause::Memory,uint16_t(65536-length));
        check(c.registers().r==old.r && (*c.context())[10]==old.flags,"test next-PC fault atomicity");
        c=machine(); c.load(65535-length,bytes); c.registers().pc=uint16_t(65535-length); c.step();
        check(!c.context() && c.registers().pc==0xffff,"test last representable next PC");
    }
}
void immediate_shifts() {
    // Independent bit-lane model: move booleans one position at a time rather
    // than duplicate the executor's integer shifts/masks.
    auto serial=[](unsigned kind,unsigned width,uint16_t input,unsigned count) {
        if (!count) return std::make_pair(input,false);
        std::array<bool,16> lane{};
        for (unsigned j=0;j<width;++j) lane[j]=(input>>j)&1;
        bool carry=false;
        for (unsigned n=0;n<count;++n) {
            bool sign=lane[width-1];
            if (kind==0 || kind==3) {
                carry=lane[width-1];
                for (unsigned j=width-1;j>0;--j) lane[j]=lane[j-1];
                lane[0]=kind==3 && carry;
            } else {
                carry=lane[0];
                for (unsigned j=0;j+1<width;++j) lane[j]=lane[j+1];
                lane[width-1]=kind==2?sign:kind==4 && carry;
            }
        }
        uint16_t output=0;
        for (unsigned j=0;j<width;++j) if (lane[j]) output|=uint16_t(1u<<j);
        return std::make_pair(output,carry);
    };
    auto flags=[](uint16_t old,unsigned bits,unsigned count,std::pair<uint16_t,bool> result) {
        return count?unsigned((old&~7)|(result.first==0?1:0)|(result.first&(1u<<(bits-1))?2:0)|(result.second?4:0)):old;
    };
    auto c=machine();
    for (unsigned kind=0;kind<5;++kind) for (unsigned width : {8u,16u})
        for (unsigned reg=0;reg<4;++reg) for (unsigned count=0;count<=16;++count)
            for (unsigned f=0;f<32;++f) for (unsigned value : {0u,1u,0x80u,0xffu,0x12abu,0x8000u,0xff00u,0xffffu}) {
                unsigned tag=0x60+8*kind+(width==16?4:0)+reg;
                code(c,{0x1a,uint8_t(tag),uint8_t(count)});
                auto& s=c.registers(); s.r.fill(0xcafe); s.r[reg]=uint16_t(value); s.flags=(f&15)|((f&16)?32:0);
                s.ds=255; s.dp=0x2345; auto old=s;
                auto expected=serial(kind,width,uint16_t(value),count); c.step();
                auto registers=old.r; registers[reg]=expected.first;
                check(!c.context() && s.r==registers && s.flags==flags(old.flags,width,count,expected) && s.pc==0x103,
                      "immediate shift/rotate all forms/counts/flags bit-lane oracle");
                check(s.ds==old.ds && s.ss==old.ss && s.cs==old.cs && s.dp==old.dp && s.sp==old.sp && s.sb==old.sb && s.st==old.st,
                      "immediate shifts preserve other state/no operand memory");
            }
    for (unsigned kind=0;kind<5;++kind) for (unsigned count=0;count<=16;++count)
        for (unsigned value=0;value<256;++value) for (unsigned carry=0;carry<2;++carry) {
            code(c,{0x1a,uint8_t(0x60+8*kind),uint8_t(count)});
            auto& s=c.registers(); s.r[0]=uint16_t(0xa500|value); s.flags=0x2b|(carry?4:0);
            auto result=serial(kind,8,s.r[0],count); unsigned expected_flags=flags(s.flags,8,count,result); c.step();
            check(s.r[0]==result.first && s.flags==expected_flags,"exhaustive byte immediate shifts/rotates");
        }
    // Closed-form word results/carry independently check the repeated-step model.
    std::mt19937 rng(606);
    for (unsigned kind=0;kind<5;++kind) for (unsigned count=1;count<=16;++count) for (unsigned i=0;i<128;++i) {
        unsigned value=rng()&65535, output=0; bool carry=false;
        if (kind==0) { output=(value<<count)&65535; carry=(value>>(16-count))&1; }
        if (kind==1) { output=value>>count; carry=(value>>(count-1))&1; }
        if (kind==2) {
            int signed_value=value<32768?int(value):int(value)-65536, divisor=1<<count;
            int quotient=signed_value>=0?signed_value/divisor:-((-signed_value+divisor-1)/divisor);
            output=uint16_t(quotient); carry=(value>>(count-1))&1;
        }
        unsigned rotate=count%16;
        if (kind==3) { output=((value<<rotate)|(value>>(16-rotate)))&65535; carry=output&1; }
        if (kind==4) { output=((value>>rotate)|(value<<(16-rotate)))&65535; carry=output&32768; }
        code(c,{0x1a,uint8_t(0x64+8*kind),uint8_t(count)}); c.registers().r[0]=uint16_t(value); c.registers().flags=0x2f;
        c.step();
        check(c.registers().r[0]==output && c.registers().flags==flags(0x2f,16,count,{uint16_t(output),carry}),"closed-form word shift/rotate oracle");
    }
    // Every encoded invalid count is an Invalid State fault after full fetch,
    // preserving the input even where a masked count would have been legal.
    for (unsigned tag=0x60;tag<=0x87;++tag) {
        for (unsigned count=17;count<256;++count) {
            c.reset(); code(c,{0x1a,uint8_t(tag),uint8_t(count)}); c.registers().r.fill(0x12ab); c.registers().flags=0x2e;
            auto old=c.registers(); c.step(); faulted(c,Cause::Invalid);
            check(c.registers().r==old.r && (*c.context())[10]==old.flags && c.registers().sp==old.sp,"invalid count atomicity");
        }
        for (unsigned missing : {1u,2u}) {
            c=machine(); code(c,{0x1a,uint8_t(tag),16}); c.map(0x100+missing,1,false);
            c.registers().r.fill(0x12ab); c.registers().flags=0x2e; auto old=c.registers(); c.step(); faulted(c,Cause::Memory);
            check(c.registers().r==old.r && (*c.context())[10]==old.flags,"truncated shift no intermediate effects");
        }
        for (unsigned count : {0u,16u,17u}) {
            c=machine(); code(c,{0x1a,uint8_t(tag),uint8_t(count)},0xfffd);
            c.registers().r.fill(0x12ab); c.registers().flags=0x2e; auto old=c.registers(); c.step();
            faulted(c,count==17?Cause::Invalid:Cause::Memory,0xfffd);
            check(c.registers().r==old.r && (*c.context())[10]==old.flags,"count validation before final PC; zero still validates PC");
        }
        c=machine(); code(c,{0x1a,uint8_t(tag),16},0xfffc); c.step();
        check(!c.context() && c.registers().pc==0xffff,"last valid immediate shift PC");
        c=machine(); code(c,{0x1a,uint8_t(tag)},0xfffe); c.step(); faulted(c,Cause::Memory,0xfffe);
        c=machine(); code(c,{0x26,1}); c.load(0x300,{0x1a,uint8_t(tag),16});
        c.step(); auto frame=*c.context(); c.registers().r[tag&3]=0x1281; c.registers().flags=0x2f; c.step();
        check(c.context() && *c.context()==frame && (c.csr()&0x68)==0x68,"immediate shifts preserve V/I/E and active frame");
    }
    // Independent published vectors: results, C at full widths, and upper bytes.
    struct Vector { uint8_t tag,count; uint16_t before,after,expected; };
    for (auto v : {Vector{0x6c,8,0x12ab,0x12,4}, {0x64,8,0x12ab,0xab00,2},
                   {0x64,16,0x12ab,0,5}, {0x6c,16,0x12ab,0,1}, {0x74,16,0x80ab,0xffff,6},
                   {0x7c,8,0x12ab,0xab12,2}, {0x84,8,0x12ab,0xab12,6},
                   {0x7c,16,0x12ab,0x12ab,4}, {0x84,16,0x12ab,0x12ab,0},
                   {0x68,9,0x1281,0,1}, {0x70,16,0x1281,0xff,6},
                   {0x78,8,0x1282,0x82,2}, {0x80,16,0x1282,0x82,6}, {0x60,0,0x12ab,0x12ab,7}}) {
        c=machine(); code(c,{0x1a,v.tag,v.count}); c.registers().r[0]=v.before; c.registers().flags=0x2f; c.step();
        check(c.registers().r[0]==v.after && c.csr()==(0x28|v.expected),"published immediate shift/rotate vector");
    }
    c=machine(); code(c,{0x1a,0x64,16}); c.registers().r[0]=0x12ab; c.registers().flags=0x28;
    irq(c); c.step(); faulted(c,Cause::IRQ); check((*c.context())[0]==0x12ab,"IRQ observes before entire shift");
    c.step(); c.step(); check(c.registers().r[0]==0 && c.registers().pc==0x103,"one step commits all sixteen bit positions");
    irq(c); c.step(); faulted(c,Cause::IRQ,0x103);
    check((*c.context())[0]==0 && (*c.context())[10]==0x2d,"IRQ observes final result/flags only");
    c=machine();
    c.load(0x100,{0x15,0,0xab,0x12,0x11,0x1a,0x6c,8,0x15,0x50,0,0x20,0x1a,0x7d,8,0x15,0x51,2,0x20,
                 0x15,0,0xab,0x12,0x1a,0x64,16,0x15,0x50,4,0x20,0x15,0,0xab,0x80,0x1a,0x74,16,0x15,0x50,6,0x20,
                 0x15,0,0xab,0x12,0x1a,0x7c,16,0x15,0x50,8,0x20,0x1a,0x84,16,0x15,0x50,10,0x20,
                 0x15,0,0x82,0x12,0x1a,0x78,8,0x15,0x50,12,0x20,0x1a,0x80,16,0x15,0x50,14,0x20,
                 0x15,0,0xab,0x12,0x1a,0x60,0,0x15,0x50,16,0x20,1});
    unsigned steps=0;
    while (c.state()==ExecutionState::Running && steps<30) { c.step(); ++steps; }
    check(c.state()==ExecutionState::Halted && !c.context() && steps==26 && c.registers().pc==0x159 &&
          c.registers().r[0]==0x12ab && c.csr()==6,"published immediate shift/rotate program");
    const uint16_t results[]={0x12,0xab12,0,0xffff,0x12ab,0x12ab,0x82,0x82,0x12ab};
    for (unsigned i=0;i<9;++i) check(word(c,0x2000+2*i)==results[i],"published shift/rotate result word");
}
void addressing() {
    for (int dest=0;dest<5;++dest) for (int delta=-128;delta<=127;++delta)
        for (int initial : {0,1,127,128,0x7fff,0xff80,0xffff}) {
            auto c=machine(); code(c,{uint8_t(0x20+dest),uint8_t(delta)});
            auto& r=dest==4?c.registers().dp:c.registers().r[dest+4]; r=initial; c.registers().flags=0x2f;
            c.step(); int expected=initial+delta;
            if (expected<0 || expected>65535) { faulted(c,Cause::Invalid); check(r==initial,"pointer fault atomicity"); }
            else check(r==expected && c.csr()==0x2f,"pointer adjustment/flags");
        }
    for (unsigned second=0x80;second<256;++second) for (unsigned base=0;base<4;++base) {
        auto c=machine(); auto& s=c.registers(); s.cs=2; s.ds=3; s.r.fill(0x2000);
        c.load(0x20100,{0x15,uint8_t(second),uint8_t(base),2,0});
        unsigned off=(second&16)?0x2002:0x2000, addr=((second&32)?0x20000:0x30000)+off;
        c.load(addr,{0x34,0x12}); c.step();
        if (second&64) check(word(c,addr)==((second&8)?0x1200:0x2000),"B1 store/base alias");
        else check(s.r[second&7]==((second&8)?0x34:0x1234),"B1 load/base alias");
        check(s.ds==3 && s.cs==2,"segment selection");
    }
    for (int descriptor=4;descriptor<256;++descriptor) {
        auto c=machine(); code(c,{0x15,0x90,uint8_t(descriptor)},0xfffd); c.step(); faulted(c,Cause::Illegal,0xfffd);
    }
    for (int kind=0;kind<6;++kind) {
        auto c=machine(); c.registers().r[0]=0xabcd;
        if (kind==0) code(c,{0x15,0x50,0xff,0xff});
        if (kind==1) { code(c,{0x15,0x50,0xfc,0xff}); c.map(0xfffd,1,false); }
        if (kind==2) { code(c,{0x15,0x60,0xff,0xff}); c.registers().dp=0; }
        if (kind==3) code(c,{0},0xffff);
        if (kind==4) code(c,{0x15},0xffff);
        if (kind==5) { code(c,{0x15,0x00,0x34,0x12},0xfffc); }
        c.load(0xffff,{kind==3?uint8_t(0):kind==4?uint8_t(0x15):uint8_t(0x55)});
        c.step(); faulted(c,Cause::Memory,kind>=3?(kind==5?0xfffc:0xffff):0x100);
        check(c.registers().r[0]==0xabcd,"memory/final-PC fault atomicity");
        if (kind<3) check(c.peek(0xffff)==0x55,"partial store");
        if (kind==2 || kind>=3) check((*c.context())[19]==0,"invalid address must be absent");
    }
    auto c=machine(); code(c,{0x4e,0xfd,0xff},0xfffd); c.step(); check(c.registers().pc==0xfffd,"wide branch next");
    code(c,{0x1f,0x60},0xfffe); c.registers().r[4]=0x100; c.step(); check(c.registers().pc==0x100,"register JS at boundary");
    code(c,{0x10,0xd1}); c.registers().r[0]=2; c.load(0x20102,{1}); c.step();
    check(c.registers().cs==2 && c.registers().pc==0x102,"CS write uses next in new segment");
}
void stacks() {
    for (int delta=-128;delta<128;++delta) for (int sp : {0,0x1000,0x1002,0x1004,0xffff}) {
        auto c=machine(); auto& s=c.registers(); s.sb=0x1000; s.st=0x1004; s.sp=sp; s.flags=0x2f;
        code(c,{0x25,uint8_t(delta)}); c.step(); int value=sp+delta;
        if (sp<0x1000 || sp>0x1004 || value<0x1000 || value>0x1004) { faulted(c,Cause::Stack); check(s.sp==sp,"ADJSP atomicity"); }
        else check(s.sp==value && c.csr()==0x2f,"ADJSP flags/range");
    }
    auto c=machine(); auto& s=c.registers(); s.ss=2; s.sb=0x1000; s.sp=0x1002; s.st=0x1004; s.r[0]=0x1234;
    code(c,{0x1f,0}); c.step(); check(s.sp==0x1000 && word(c,0x21000)==0x1234,"push inclusive bound");
    code(c,{0x1f,0}); c.step(); faulted(c,Cause::Stack); check(s.sp==0x1000,"failed push");
    c=machine(); c.registers().sb=0x1000; c.registers().sp=c.registers().st=0x10c8;
    code(c,{0x25,0x80,0x25,0x80}); c.step(); c.step(); faulted(c,Cause::Stack,0x102);
    check((*c.context())[11]==0x1048,"no sequence rollback");
    c=machine(); code(c,{0x1f,0x10,0,2}); c.load(0x200,{0x1f,0x11}); c.step();
    check(word(c,0xfffc)==0x104 && c.registers().sp==0xfffc,"CALL return word"); c.step();
    check(c.registers().pc==0x104 && c.registers().sp==0xfffe,"RET");
    c=machine(); code(c,{0x1f,0x10,0,2}); c.map(0x200,1,false); c.load(0xfffc,{0xaa,0xbb}); c.step();
    faulted(c,Cause::Memory); check(word(c,0xfffc)==0xbbaa && c.registers().sp==0xfffe,"CALL failure atomicity");
    c=machine(); code(c,{0x15,0x30,0xff,0xff}); c.step(); faulted(c,Cause::Stack);
}
void far_calls() {
    for (unsigned source=0;source<4;++source) for (unsigned dest=0;dest<4;++dest)
        for (unsigned f=0;f<32;++f) {
            auto c=machine(); auto& s=c.registers();
            s.cs=source; s.ds=2; s.ss=3; s.sb=0x1000; s.sp=s.st=0x1004; s.dp=0x4321;
            s.r.fill(0x1234); s.flags=(f&15)|((f&16)?32:0); auto old=s;
            c.load((source<<16)|0x100,{0x54,0,2,uint8_t(dest),0});
            c.load((dest<<16)|0x200,{0x55}); c.step();
            check(!c.context() && s.cs==dest && s.pc==0x200 && s.sp==0x1000,"CALLF segment/PC/SP commit");
            check(word(c,0x31000)==0x105 && word(c,0x31002)==source,"far frame PC then canonical CS");
            check(s.r==old.r && s.flags==old.flags && s.ds==old.ds && s.ss==old.ss && s.dp==old.dp && s.sb==old.sb && s.st==old.st,"CALLF preserved state");
            c.step();
            check(!c.context() && s.cs==source && s.pc==0x105 && s.sp==old.sp && s.flags==old.flags && s.r==old.r && s.ds==old.ds && s.ss==old.ss,"RETF round trip");
        }
    auto unchanged=[](const VirtualCPU& c,const Registers& old) {
        check(c.context() && (*c.context())[8]==old.pc && (*c.context())[9]==old.cs &&
              (*c.context())[10]==old.flags && (*c.context())[11]==old.sp,"far fault saved original state");
        check(c.registers().sp==old.sp && c.registers().r==old.r && c.registers().ds==old.ds &&
              c.registers().ss==old.ss && c.registers().dp==old.dp,"far fault no ordinary effects");
    };
    auto setup=[](bool call) {
        auto c=machine(); auto& s=c.registers();
        code(c,{uint8_t(call?0x54:0x55),0,2,1,0});
        s.ss=2; s.sb=0x1000; s.st=0x1004; s.sp=call?0x1004:0x1000; s.flags=0x2f; s.r.fill(0x12ff);
        c.load(0x21000,{0,2,1,0}); c.load(0x10200,{0x55}); return c;
    };
    for (bool call : {false,true}) for (unsigned missing=0;missing<4;++missing) {
        auto c=setup(call); auto old=c.registers();
        c.map(0x21000+missing,1,false); c.step(); faulted(c,Cause::Memory); unchanged(c,old);
        check(word(c,0x21000)==0x200 && word(c,0x21002)==1,"failed far transfer preserves all frame bytes");
        check((*c.context())[18]==0x1000+missing && (*c.context())[19]==0x102,"first missing frame byte");
    }
    for (bool call : {false,true}) {
        auto c=setup(call); auto old=c.registers(); c.map(0x10200,1,false); c.step();
        faulted(c,Cause::Memory); unchanged(c,old);
        check(word(c,0x21000)==0x200 && word(c,0x21002)==1,"invalid target cannot write/pop far frame");
        for (unsigned scenario=0;scenario<4;++scenario) {
            c=setup(call); auto& s=c.registers();
            if (scenario==0) s.sb=s.sp+1;
            if (scenario==1) s.st=s.sp-1;
            if (scenario==2) s.sp=call?0x1003:0x1001;
            if (scenario==3) { s.sb=0; s.sp=call?0:0xfffe; s.st=0xffff; }
            old=s; c.step(); faulted(c,Cause::Stack); unchanged(c,old);
        }
    }
    for (unsigned high=1;high<256;++high) for (bool call : {false,true}) {
        auto c=setup(call);
        if (call) { c.load(0x104,{uint8_t(high)}); c.registers().sb=0x1005; }
        else { c.load(0x21003,{uint8_t(high)}); c.map(0x10200,1,false); }
        auto old=c.registers(); c.step(); faulted(c,Cause::Invalid); unchanged(c,old);
    }
    for (unsigned missing=1;missing<5;++missing) {
        auto c=setup(true); c.registers().sb=0x1005; c.map(0x100+missing,1,false);
        auto old=c.registers(); c.step(); faulted(c,Cause::Memory); unchanged(c,old);
    }
    // Frame availability wins over a noncanonical saved CS, and stack/operand
    // checks precede CALLF's resulting-PC range and target checks.
    auto c=setup(false); c.load(0x21003,{1}); c.map(0x21000,1,false); c.step(); faulted(c,Cause::Memory);
    c=setup(true); code(c,{0x54,0,2,1,0},0xfffb); c.map(0x21003,1,false); c.step();
    faulted(c,Cause::Memory,0xfffb); check((*c.context())[18]==0x1003,"frame failure before following PC");
    c=setup(true); code(c,{0x54,0,2,1,0},0xfffb); auto old=c.registers(); c.step();
    faulted(c,Cause::Memory,0xfffb); unchanged(c,old);
    check((*c.context())[19]==0 && word(c,0x21000)==0x200,"unrepresentable saved next is precise and atomic");
    for (unsigned pc=0xfffc;pc<=0xffff;++pc) {
        c=setup(true); code(c,{0x54,0,2,1,0},uint16_t(pc)); old=c.registers(); c.step();
        faulted(c,Cause::Memory,uint16_t(pc)); unchanged(c,old);
        check((*c.context())[19]==0,"CALLF fetch never crosses into another segment");
    }
    c=setup(true); code(c,{0x54,0,2,1,0},0xfffa); c.step(); c.step();
    check(!c.context() && c.registers().pc==0xffff && c.registers().cs==0 && c.registers().sp==0x1004,"largest saved return offset");
    c=setup(false); code(c,{0x55},0xffff); c.step();
    check(!c.context() && c.registers().cs==1 && c.registers().pc==0x200 && c.registers().sp==0x1004,"RETF ignores unused unrepresentable next");
    c=setup(false); c.load(0x21000,{0,0,0,0}); c.step();
    check(!c.context() && c.registers().pc==0 && c.registers().cs==0 && c.registers().sp==0x1004,"RETF target offset zero");
    c=setup(true); code(c,{0x54,0xff,0xff,1,0}); c.load(0x1ffff,{0x55}); c.step(); c.step();
    check(!c.context() && c.registers().pc==0x105 && c.registers().cs==0 && c.registers().sp==0x1004,"CALLF target offset FFFF executes RETF");
    c=setup(true); c.registers().sp=c.registers().st=0xffff; c.registers().sb=0xfffb;
    c.step(); check(c.registers().sp==0xfffb && word(c,0x2fffb)==0x105 && word(c,0x2fffd)==0,"odd far frame at final allocated bytes");
    c.step(); check(c.registers().sp==0xffff && !c.context(),"odd far frame return");
    // Recursive far calls: four far frames, each kept distinct until its RETF.
    c=machine(); code(c,{0x54,0,2,1,0,1}); c.registers().r[0]=3; c.registers().r[1]=1;
    c.load(0x10200,{0x10,0x20,0x40,7,0,0x10,0x74,0x54,0,2,1,0,0x55});
    unsigned steps=0,minimum=0xfffe;
    while (c.state()==ExecutionState::Running && steps<100) { c.step(); ++steps; minimum=std::min(minimum,unsigned(c.registers().sp)); }
    check(c.state()==ExecutionState::Halted && !c.context() && steps==20 && c.registers().r[0]==0 &&
          c.registers().sp==0xfffe && minimum==0xffee,"recursive far calls");
    // Published two-image program: CALLF -> near CALL/RET -> RETF.
    c=machine(); c.load(0x100,{0x15,0,5,0,0x15,1,1,0,0x54,0,2,1,0,1});
    c.load(0x10200,{0x1f,0x10,5,2,0x55,0x10,0x64,0x1f,0x11}); steps=0; minimum=0xfffe;
    while (c.state()==ExecutionState::Running && steps<100) { c.step(); ++steps; minimum=std::min(minimum,unsigned(c.registers().sp)); }
    check(c.state()==ExecutionState::Halted && !c.context() && steps==8 && c.registers().cs==0 &&
          c.registers().pc==0x10e && c.registers().r[0]==6 && c.registers().sp==0xfffe && minimum==0xfff8,"published far/near mixed program");
    c=machine(); code(c,{0x1f,0x10,0,2,1});
    c.load(0x200,{0x54,0,3,1,0,0x1f,0x11}); c.load(0x10300,{0x55});
    for (unsigned i=0;i<5;++i) c.step();
    check(c.state()==ExecutionState::Halted && !c.context() && c.registers().pc==0x105 &&
          c.registers().cs==0 && c.registers().sp==0xfffe,"near CALL containing far CALLF/RETF");
    // IRQs observe either the uncalled state or the entire committed frame.
    c=setup(true); c.registers().sb=0x0ffe; irq(c); c.step(); faulted(c,Cause::IRQ);
    check((*c.context())[11]==0x1004 && word(c,0x21000)==0x200,"IRQ before CALLF");
    c.step(); c.step(); irq(c); c.step(); faulted(c,Cause::IRQ,0x200);
    check((*c.context())[9]==1 && (*c.context())[11]==0x1000 && word(c,0x21000)==0x105,"IRQ after complete CALLF");
    c.step(); c.step(); check(!c.context() && c.registers().cs==0 && c.registers().pc==0x105 && c.registers().sp==0x1004,"IRQ/IRET then RETF");
    irq(c); c.step(); faulted(c,Cause::IRQ,0x105);
    check((*c.context())[9]==0 && (*c.context())[11]==0x1004,"IRQ after complete RETF pop");
    c=machine(); code(c,{0x26,1}); c.load(0x300,{0x54,0,2,1,0,0x1f,0x12}); c.load(0x10200,{0x55});
    c.step(); auto saved=*c.context(); c.step(); c.step();
    check(c.context() && *c.context()==saved && c.csr()==0x40 && c.registers().pc==0x305,"far call in active handler preserves E/frame");
    c.step(); check(!c.context() && c.registers().pc==0x102,"handler returns using IRET");
    // Both ends of the full eight-bit CS range, with no narrowing to a smaller bank.
    VirtualCPU wide; vector_to(wide,0,0x100); wide.reset();
    control(wide,0x20,0); control(wide,0x21,255); control(wide,0x22,3); control(wide,0x23,0x1c1);
    control(wide,0x24,0); control(wide,0x25,0); control(wide,0x26,1); control(wide,0x27,0); control(wide,0x28,0); control(wide,0x7f,1);
    wide.load(0x100,{0x54,0,2,0xff,0}); wide.load(0x30200,{0x55}); wide.step(); wide.step();
    check(!wide.context() && wide.registers().pc==0x105 && wide.registers().cs==0,"target segment FF");
    wide.registers().cs=255; wide.registers().pc=0x100; wide.load(0x30100,{0x54,0,2,0,0}); wide.load(0x200,{0x55});
    wide.step(); check(word(wide,0xfffc)==0xff,"saved canonical CS FF"); wide.step();
    check(!wide.context() && wide.registers().cs==255 && wide.registers().pc==0x105,"return segment FF");
    c=setup(true); c.load(0x10200,{0xff}); c.step();
    check(!c.context() && c.registers().cs==1 && c.registers().sp==0x1000,"CALLF probes target without decoding");
    c.step(); faulted(c,Cause::Illegal,0x200);
}
void traps() {
    for (unsigned service=0;service<256;++service) for (unsigned flags=0;flags<32;++flags) {
        auto c=machine(); code(c,{0x26,uint8_t(service)}); auto& r=c.registers();
        r.flags=(flags&15)|((flags&16)?32:0); r.r[4]=0x9876;r.dp=0x5678;r.ds=3;r.ss=2;
        auto old=r;c.step();faulted(c,Cause::Trap,0x102);
        check((*c.context())[20]==service && (*c.context())[10]==old.flags,"SWI service and saved flags");
        check(c.csr()==((old.flags&15)|64) && c.protected_state()->frames.size()==1,"protected SWI entry");
        r.r.fill(0xabcd);r.ds=0;r.ss=0;r.dp=0;c.step();
        check(!c.context() && c.csr()==old.flags && r.pc==0x102 && r.r==old.r && r.dp==old.dp && r.ds==3 && r.ss==2,"full IRET restore");
    }
    auto c=machine();code(c,{0x26,1});c.load(0x300,{0x26,2});c.step();auto outer=c.protected_state()->frames.front().words;c.step();
    check(c.protected_state()->frames.size()==2 && c.protected_state()->frames.front().words==outer && (*c.context())[20]==2,"nested SWI retains outer frame");
}
void interrupts_reset() {
    auto c=machine();code(c,{3,2,1});irq(c);c.step();
    check(c.registers().pc==0x101 && !c.context(),"EI boundary");c.step();faulted(c,Cause::IRQ,0x101);
    check(!c.irq_pending() && c.protected_state()->in_service==2,"IRQ moves selected edge into service");
    c.step();check(!c.context() && c.registers().pc==0x101,"IRQ returns to interrupted boundary");
    c=machine();code(c,{1,0});c.registers().flags=32;c.step();
    check(c.state()==ExecutionState::Halted && c.registers().pc==0x101,"HALT continuation");irq(c);c.step();faulted(c,Cause::IRQ,0x101);c.step();
    check(c.state()==ExecutionState::Running && c.registers().pc==0x101,"HALT wake return");
    c.load(0x2000,{0xaa});c.reset();
    check(c.state()==ExecutionState::Running && !c.context() && !c.irq_pending() && c.peek(0x2000)==0xaa,"reset CPU/device state retains RAM");
    c.map(0,1,false);c.reset();check(c.state()==ExecutionState::TerminalResetFault && !c.context(),"reset vector fault");
    check(!c.step(),"terminal reset does not step");c.map(0,1,true);c.reset();check(c.state()==ExecutionState::Running,"reset recovery");
}
void decode_and_recovery() {
    for (unsigned reg=0;reg<256;++reg) {
        auto c=machine();code(c,{0x1f,0x34,uint8_t(reg)});c.step();faulted(c,reg<8?Cause::Invalid:Cause::Illegal);
    }
    for (unsigned index=0;index<21;++index) for (unsigned reg=0;reg<8;++reg) {
        auto c=machine();code(c,{0x26,0xab});c.load(0x300,{0x1f,uint8_t(0x20+index),uint8_t(reg)});
        c.step();auto expected=(*c.context())[index];c.step();check(c.registers().r[reg]==expected,"GETX every base field/register");
    }
    auto c=machine();code(c,{0xff,0});c.load(0x300,{0x15,0,1,1,0x1f,0x48,0,0x15,0,5,0,0x1f,0x4a,0,0x1f,0x12});
    c.registers().r[0]=0xbeef;c.step();for(unsigned i=0;i<5;++i)c.step();
    check(c.registers().pc==0x101 && c.csr()==5 && c.registers().r[0]==0xbeef,"saved resume PC/CSR recovery");
}
void worked_images() {
    std::ifstream file(SOURCE_ROOT "/doc/ISA_EXAMPLES.md"); std::string line;
    std::vector<std::vector<uint8_t>> images; bool block=false; std::vector<uint8_t> image;
    std::regex row("^([0-9A-F]{4})  ((?:[0-9A-F]{2} ?)+) +(.+)$");
    while (std::getline(file,line)) {
        if (line.find("<!-- WORKED_IMAGES_END -->")!=std::string::npos) break;
        if (line=="```text") { block=true; image.clear(); continue; }
        if (line=="```" && block) { block=false; images.push_back(image); continue; }
        std::smatch m;
        if (block && std::regex_match(line,m,row)) {
            std::istringstream bytes(m[2]); std::string byte;
            while (bytes>>byte) image.push_back(uint8_t(std::stoul(byte,nullptr,16)));
        }
    }
    check(images.size()==12,"published fixture extraction");
    const int steps[]={57,33,142,42,92,9,48,21,22,6};
    for (unsigned image_index=0;image_index<10;++image_index) {
        unsigned n=image_index%10;
        auto c=machine(); c.load(0x100,images[n]);
        if (n==1) c.load(0x2000,{1,0,2,0,3,0,4,0,5,0});
        if (n==2) c.load(0x2000,{3,0,0xff,0xff,2,0,0xfd,0xff,0,0});
        if (n==3) c.load(0x2000,{'h','e','l','l','o',0});
        if (n==6) { c.load(0x32000,{'h','e','l','l','o',0}); c.load(0x300,{0x55,0}); }
        if (n==7) c.load(0xfffd,{3,4,5});
        if (n==9) c.load(0x300,{0x0e,1});
        unsigned count=0; uint16_t minimum=0xfffe;
        while (c.state()==ExecutionState::Running && count<10000) { c.step(); ++count; minimum=std::min(minimum,c.registers().sp); }
        check(c.state()==ExecutionState::Halted && !c.context() && count==unsigned(steps[n]),"worked program "+std::to_string(n)+" instruction count");
        if (n==0 || n==1 || n==4 || n==7 || n==9) {
            uint16_t expected[]={55,15,0,0,120,0,0,12,0,0xbeef}; check(word(c,n==1?0x2100:0x2000)==expected[n],"worked scalar result "+std::to_string(n));
        }
        if (n==2) for (unsigned i=0;i<5;++i) check(word(c,0x2000+2*i)==uint16_t(int(i==0?-3:i==1?-1:i==2?0:i==3?2:3)),"sorted words");
        if (n==3 || n==6) {
            unsigned base=n==3?0:0x30000;
            for (unsigned i=0;i<6;++i) check(c.peek(base+0x2100+i)==c.peek(base+0x2000+i),"copied string");
        }
        if (n==4) check(minimum==0xffea && c.registers().sp==0xfffe,"recursive stack");
        if (n==5) check(word(c,0x2000)==0 && word(c,0x2002)==4,"multiword carry");
        if (n==6) check(word(c,0x32108)==0x55 && c.registers().r[4]==0x2005 && c.registers().r[5]==0x2105 && c.registers().r[6]==0x300,"CS/DS final pointers");
        if (n==7) check(c.registers().r[4]==0xffff,"last-byte traversal");
        if (n==8) check(word(c,0x2000)==14 && word(c,0x2002)==0x1111 && word(c,0x2004)==0x2222 && word(c,0x3000)==7 && word(c,0x3002)==7 && minimum==0xffec && c.registers().sp==0xfffe,"framed call");
    }
    for (int service : {1,255}) {
        auto c=machine(); c.load(0x100,images[10]); c.load(0x10200,images[11]); vector_to(c,6,0x10200);
        c.load(0x107,{uint8_t(service)}); unsigned count=0;
        while (c.state()==ExecutionState::Running && count++<100) c.step();
        check(!c.context() && c.state()==ExecutionState::Halted && c.registers().pc==0x109 && c.registers().r[0]==(service==1?12:0) && c.registers().r[1]==(service==1?0:1),"cross-segment syscall image");
    }
}
int main(int argc, char**) {
    std::string section;
    auto run=[&](const char* name,void (*test)()){section=name;test();};
    try {
        if(argc==1) {
            run("documented tests",documented_tests);run("arithmetic",arithmetic);
            run("bit tests",bit_tests);run("immediate shifts",immediate_shifts);
            run("addressing",addressing);run("stacks",stacks);run("far calls",far_calls);
            run("traps",traps);run("interrupts/reset",interrupts_reset);
            run("decode/recovery",decode_and_recovery);
        }
        run("published examples",worked_images);
    } catch(const std::exception& e) {
        std::cerr<<"FAIL in "<<section<<" after "<<checks<<" checks: "<<e.what()<<'\n';return 1;
    }
    std::cout<<checks<<" conformance assertions passed\n";
}
