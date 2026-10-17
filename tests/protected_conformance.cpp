#include "cpu.hpp"
#include "kernel_images.hpp"
#include <algorithm>
#include <iostream>
#include <limits>
#include <sstream>
#include <stdexcept>
using namespace vcpu;
namespace {
size_t checks=0;
void check(bool value,const std::string& label) { ++checks; if (!value) throw std::runtime_error(label); }
const ProtectedState& ps(const VirtualCPU& c) { return *c.protected_state(); }
void vector_to(VirtualCPU& c,unsigned n,uint32_t target) { c.load(4*n,{uint8_t(target),uint8_t(target>>8),uint8_t(target>>16),0}); }
void execute(VirtualCPU& c,std::vector<uint8_t> bytes,uint16_t pc=0x700) {
    c.load(pc,bytes); c.registers().pc=pc; c.registers().cs=0;
    check(c.step(),"guest instruction made no progress");
}
void setc(VirtualCPU& c,uint8_t id,uint16_t value) { c.registers().r[7]=value; execute(c,{0x1f,0xb7,id}); }
uint16_t getc(VirtualCPU& c,uint8_t id) { execute(c,{0x1f,0xa7,id}); return c.registers().r[7]; }
void setx(VirtualCPU& c,uint8_t id,uint16_t value) {
    c.registers().r[7]=value;
    execute(c,id<0x20?std::vector<uint8_t>{0x1f,uint8_t(0x40+id),7}:std::vector<uint8_t>{0x1f,0x97,id});
}
uint16_t getx(VirtualCPU& c,uint8_t id) {
    execute(c,id<0x20?std::vector<uint8_t>{0x1f,uint8_t(0x20+id),7}:std::vector<uint8_t>{0x1f,0x87,id});
    return c.registers().r[7];
}
void descriptor(VirtualCPU& c,unsigned map,unsigned seg,std::array<uint16_t,7> d) {
    setc(c,0x20,map); setc(c,0x21,seg);
    for (unsigned i=0;i<7;++i) setc(c,0x22+i,d[i]);
    setc(c,0x7f,1);
}
VirtualCPU machine() {
    VirtualCPU c(Revision::Working,0x80000);
    for (unsigned i=0;i<8;++i) vector_to(c,i,i?0x300:0x100);
    c.load(0x300,{0x1f,0x12}); c.reset();
    for (unsigned i=0;i<5;++i) setc(c,0x10+i,i<3?0:0xfffe);
    setc(c,0x7f,5); return c;
}
void user_map(VirtualCPU& c,unsigned map=7,unsigned frame=1) {
    descriptor(c,map,1,{uint16_t(frame),0x21,0x100,0x200,0,0,0});
    descriptor(c,map,2,{5,0x1d,0x1000,0x2000,0,0x1000,0x2000});
    descriptor(c,map,3,{3,0x19,0,0,1,0,0});
    setc(c,0x7f,3);
}
void user_return(VirtualCPU& c,unsigned map=7) {
    execute(c,{0x26,0});
    setx(c,8,0x100); setx(c,9,1); setx(c,10,32); setx(c,11,0x2000);
    setx(c,12,0x1000); setx(c,13,0x2000); setx(c,14,2); setx(c,16,3);
    setx(c,0x20,map); setx(c,0x21,0); setx(c,0x22,0);
    setc(c,0x7f,7); execute(c,{0x1f,0x12});
    check(c.mode()==Mode::User && ps(c).frames.empty(),"bootstrap U return");
}
void fault(const VirtualCPU& c,Cause cause) {
    check(c.context() && (*c.context())[17]==unsigned(cause),"wrong protected cause");
    check(c.state()==ExecutionState::Running,"exception entry unexpectedly terminal");
}
uint16_t word(const VirtualCPU& c,unsigned a) { return c.peek(a)|(unsigned(c.peek(a+1))<<8); }
void mmio_write(VirtualCPU& c,unsigned offset,uint16_t value) {
    c.registers().ds=255; c.registers().r[0]=value;
    execute(c,{0x15,0x50,uint8_t(offset),uint8_t(offset>>8)}); c.registers().ds=0;
}
uint16_t mmio_read(VirtualCPU& c,unsigned offset) {
    c.registers().ds=255; execute(c,{0x15,0x10,uint8_t(offset),uint8_t(offset>>8)});
    c.registers().ds=0; return c.registers().r[0];
}
BoundaryEvent event(const VirtualCPU& c,BoundaryEvent::Kind kind,unsigned source=0) {
    BoundaryEvent e{c.run(),c.boundary(),c.next_sequence(),kind}; e.source=source; return e;
}
void resume(VirtualCPU& c) { setx(c,8,0x800); execute(c,{0x1f,0x12}); }
void immediate_alu() {
    auto c=machine();
    for (unsigned kind=0;kind<6;++kind) for (unsigned bits:{8u,16u}) for (unsigned reg=0;reg<4;++reg) {
        unsigned mask=(1u<<bits)-1,sign=1u<<(bits-1);
        std::vector<unsigned> values;
        if (bits==8) for (unsigned i=0;i<256;++i) values.push_back(i);
        else values={0,1,2,127,128,255,256,0x7ffe,0x7fff,0x8000,0x8001,0xfffe,0xffff};
        for (unsigned a:values) for (unsigned b:values) {
            uint16_t original=bits==8?0x1200|a:a;
            c.registers().r[reg]=original; c.registers().flags=0x2f;
            unsigned result=kind==0?(a+b)&mask:kind<3?(a-b)&mask:kind==3?a&b:kind==4?a|b:a^b;
            unsigned flags=32|(result==0?1:0)|(result&sign?2:0);
            if (kind<3) {
                bool sub=kind!=0;
                bool carry=sub?a>=b:a+b>mask;
                bool overflow=sub?((a^b)&(a^result)&sign):(~(a^b)&(a^result)&sign);
                flags|=(carry?4:0)|(overflow?8:0);
            } else flags|=12;
            uint8_t op=0x80+kind*8+(bits==16?4:0)+reg;
            execute(c,bits==8?std::vector<uint8_t>{op,uint8_t(b)}:std::vector<uint8_t>{op,uint8_t(b),uint8_t(b>>8)});
            check(c.registers().r[reg]==(kind==2?original:result) && c.csr()==flags,"immediate ALU value/width/flags");
        }
    }
    execute(c,{0x26,0}); c.registers().r[0]=0x1280; execute(c,{0x90,0x80});
    check(c.registers().r[0]==0x1280 && (c.csr()&0x41)==0x41,"CMPI byte equality and E preservation");
    execute(c,{0x40,5,0}); check(c.registers().pc==0x708,"BEQ after byte CMPI");
    c.registers().r[0]=0x127f; execute(c,{0x90,0x80});
    execute(c,{0x4b,5,0}); check(c.registers().pc==0x703,"signed byte BLT evaluates N xor V");
    execute(c,{0x43,5,0}); check(c.registers().pc==0x708,"unsigned byte BCC evaluates no-borrow");
    c.registers().r[0]=0x7fff; execute(c,{0x94,0,0x80});
    execute(c,{0x4b,5,0}); check(c.registers().pc==0x703,"signed word BLT interprets word flags");
}
void controls_and_maps() {
    auto c=machine();
    for (unsigned id:{0u,1u,2u,3u,4u,5u,0x10u,0x11u,0x12u,0x13u,0x14u,0x18u,0x19u,0x1au,0x1bu,0x1cu,
            0x20u,0x21u,0x22u,0x23u,0x24u,0x25u,0x26u,0x27u,0x28u,0x29u,0x30u,0x31u,0x32u,0x33u,0x34u,0x35u,0x36u,0x40u,0x41u,0x42u,0x43u,0x44u,0x45u}) {
        for (unsigned reg=0;reg<8;++reg) {
            execute(c,{0x1f,uint8_t(0xa0+reg),uint8_t(id)});
            check(!c.context(),"all readable controls in all GPRs");
        }
    }
    user_map(c);
    check(getc(c,0x29)==1 && getc(c,0x30)==3,"published descriptor committed readback");
    setc(c,0x22,9); check(getc(c,0x30)==3 && getc(c,0x22)==9,"shadow changes do not publish");
    execute(c,{0x26,17}); setc(c,0x22,8); execute(c,{0x1f,0x12});
    check(getc(c,0x22)==8,"staging survives interrupted handler and is shared");
    setc(c,0x7f,2); check(getc(c,0x22)==3,"DESC_LOAD restores committed shadow");
    auto active=ps(c).descriptors.at(0x703);
    setc(c,0x24,0xffff); setc(c,0x25,0); setc(c,0x26,0); setc(c,0x7f,1); fault(c,Cause::Invalid);
    check(ps(c).descriptors.at(0x703)==active,"invalid descriptor commit atomic"); resume(c);
    setc(c,0x7f,4); check(!ps(c).published[7] && ps(c).descriptors.count(0x703)==0 && getc(c,0x22)==0,"invalidate clears map and selected shadow");
    setc(c,0x7f,3); check(ps(c).published[7],"empty map publish legal");
    user_map(c); check(ps(c).descriptors.at(0x703)==active,"map identifier reuse");
    setc(c,0x20,0); setc(c,0x7f,4); fault(c,Cause::Invalid); check(ps(c).published[0],"map zero cannot invalidate"); resume(c);
    for (unsigned id:{0u,1u,2u,0x10u,0x11u,0x20u,0x21u,0x22u,0x41u}) {
        setc(c,id,256); fault(c,Cause::Invalid); resume(c);
    }
    for (unsigned command:{0u,9u,65535u}) { setc(c,0x7f,command); fault(c,Cause::Invalid); resume(c); }
    setc(c,0x40,0xff80); setc(c,0x41,7); setc(c,0x7f,8);
    check(getc(c,0x42)==1 && getc(c,0x43)==128 && getc(c,0x44)==0 && getc(c,0x45)==1,"backing query installed run");
    setc(c,0x40,0); setc(c,0x41,255); setc(c,0x7f,8);
    check(getc(c,0x42)==2 && getc(c,0x43)==256,"backing query MMIO run");
    setc(c,0x40,256); setc(c,0x7f,8); check(getc(c,0x42)==0 && getc(c,0x43)==0xff00,"backing query final hole run");
    auto tuple=ps(c).entry; setc(c,0x12,0xfffe); setc(c,0x7f,5); fault(c,Cause::Stack);
    check(ps(c).entry==tuple,"ENTRY_COMMIT next push required and old tuple retained"); resume(c); setc(c,0x7f,6);
    check(ps(c).entry_shadow==tuple,"ENTRY_LOAD restores active tuple");
    // A legal control action at the segment end must not publish its staged effect.
    c.registers().r[0]=123; execute(c,{0x1f,0xb0,0x24},0xfffd); fault(c,Cause::Memory);
    check(ps(c).shadow[2]!=123 && ps(c).frames.back().memory[1]==3,"failed next PC leaves controls untouched");
}
void protection_boundaries() {
    for (auto bad:std::vector<std::array<uint16_t,7>>{{1,0,0,0,0,0,0},{0,1,0,1,1,0,0},
            {0,1,2,1,0,0,0},{0,1,0,0,1,1,0},{0,5,0,0x2000,0,0x2001,0x2001},
            {255,0x23,0,0,1,0,0},{255,0x0b,0,0,1,0,0},{255,0x107,0,0,1,0,0}}) {
        auto c=machine(); descriptor(c,7,255,bad); fault(c,Cause::Invalid);
        check(ps(c).descriptors.count(0x7ff)==0,"descriptor rejects invalid bounds/kind/stack/absent state");
    }
    auto c=machine(); descriptor(c,0,3,{3,0x19,0,0,1,0,0}); fault(c,Cause::Invalid);
    check(ps(c).descriptors.at(3)[1]==0x1c1,"map zero cannot grant user rights");
    c=machine(); descriptor(c,255,255,{7,0x19,0,0,1,0,0}); setc(c,0x7f,3); setc(c,1,255); setc(c,2,255);
    c.registers().r[4]=0xffff; c.load(0x7ffff,{0xa5}); execute(c,{0x27,0x20,0});
    check(c.registers().r[0]==0xa5 && !c.context(),"selector/map 255 and full-segment bound 65536");
    for (unsigned rights=0;rights<4;++rights) for (unsigned store=0;store<2;++store) {
        auto d=machine(); user_map(d); descriptor(d,7,3,{3,uint16_t(1|(rights<<3)),0,0,1,0,0});
        setc(d,1,7); setc(d,2,3); d.registers().r[4]=0x2000;
        execute(d,{0x27,uint8_t(store?128:0),0});
        bool permitted=rights&(store?2:1);
        if (!permitted) { fault(d,Cause::Memory); check(ps(d).frames.back().memory[10]==6,"independent UR/UW rights"); }
        else check(!d.context(),"granted user right permits transfer");
    }
    auto u=machine(); user_map(u); descriptor(u,7,4,{2,0x21,0x100,0x200,0,0,0}); user_return(u);
    u.load(0x10100,{0x54,0,1,4,0}); u.load(0x20100,{0x55}); u.step();
    check(u.mode()==Mode::User && u.registers().cs==4 && u.registers().sp==0x1ffc && word(u,0x51ffc)==0x105 && word(u,0x51ffe)==1,"U far call checks UX and writes exact frame");
    u.step(); check(u.registers().cs==1 && u.registers().pc==0x105 && u.registers().sp==0x2000,"U far return");
    u.load(0x10105,{0x54,0,0,3,0}); u.step(); fault(u,Cause::Memory);
    check((*u.context())[9]==1 && (*u.context())[11]==0x2000 && ps(u).frames.back().memory[10]==6 && ps(u).frames.back().memory[1]==4,"nonexecutable far target preserves caller frame state");
    u=machine(); user_map(u); descriptor(u,7,4,{6,0x1d,0x100,0x300,0,0x120,0x280}); user_return(u);
    u.registers().r[0]=4; u.load(0x10100,{0x10,0xd0}); u.step();
    check(u.registers().ss==4 && u.registers().sb==0x120 && u.registers().st==0x280 && u.registers().sp==0x280,"U SS installs stack descriptor policy");
    u=machine(); user_map(u); user_return(u); u.load(0x10100,{0x15,0xa0,0}); u.registers().r[4]=0x100; u.step(); fault(u,Cause::Memory);
    check(ps(u).frames.back().memory[10]==6,"execute-only user code does not imply read permission");
    u=machine(); user_map(u); descriptor(u,7,3,{255,0x19,0,0,1,0,0}); user_return(u);
    u.load(0x10100,{0x15,0x10,0,0}); u.step(); fault(u,Cause::Memory);
    check(ps(u).device_commits==0 && ps(u).frames.back().memory[10]==7,"ordinary U mapping of MMIO denied without callback");
    // Temporarily replace a pinned U return image with a bad candidate. A nested
    // IRET fault returns to a K rollback continuation; the original image survives.
    u=machine(); user_map(u); user_return(u); u.load(0x10100,{0x26,1}); u.step();
    auto original=ps(u).frames.back(); setx(u,8,0x500); execute(u,{0x1f,0x12}); fault(u,Cause::Memory);
    check(ps(u).frames.size()==2 && ps(u).frames.back().memory[1]==6 && ps(u).frames.front().words[8]==0x500,"candidate return failure preserves lower image for rollback");
    setx(u,8,0x800); execute(u,{0x1f,0x12});
    setx(u,8,original.words[8]); execute(u,{0x1f,0x12});
    check(u.mode()==Mode::User && !u.context() && u.registers().pc==original.words[8] && ps(u).controls==original.controls,"rollback restores original process and return PC");
}
void context_and_privilege() {
    auto c=machine(); execute(c,{0x26,0xa5}); auto base=*c.context();
    for (unsigned id=0;id<=0x14;++id) check(getx(c,id)==base[id],"legacy context readback");
    for (unsigned id=0x20;id<=0x29;++id) {
        unsigned expected=id==0x23 || id==0x24 || id==0x26?1:0;
        check(getx(c,id)==expected,"extended provenance selector");
    }
    for (unsigned id=0x30;id<=0x3a;++id) check(getx(c,id)==0,"nonmemory context words are zero");
    for (unsigned id=0;id<=16;++id) { auto old=getx(c,id); setx(c,id,0x1234); check(getx(c,id)==0x1234,"editable base word"); setx(c,id,old); }
    for (unsigned id=0x20;id<=0x22;++id) { setx(c,id,0x1234); check(getx(c,id)==0x1234,"extended edits defer validation"); setx(c,id,0); }
    setx(c,9,256); execute(c,{0x1f,0x12}); fault(c,Cause::Invalid);
    check(ps(c).frames.size()==2 && ps(c).frames.front().words[9]==256,"invalid IRET retains lower image");
    resume(c); setx(c,9,0); resume(c); check(!c.context(),"repair nested invalid IRET then return");
    for (unsigned depth=1;depth<=8;++depth) {
        execute(c,{0x26,uint8_t(depth)});
        check(ps(c).frames.size()==depth && getx(c,0x25)==depth-1 && getx(c,0x26)==depth,"depth and nested capture");
    }
    auto prior=c.registers(); execute(c,{0x26,9});
    check(c.state()==ExecutionState::TerminalDoubleFault && ps(c).frames.size()==8,"ninth frame terminal capacity");
    auto diagnostic=*c.terminal_diagnostic();
    check(!diagnostic.captured && diagnostic.reason=="capacity" && diagnostic.stage=="reserve" && diagnostic.registers.pc==0x700,"terminal capacity diagnostic");
    check(!c.step() && c.terminal_diagnostic()->pc==diagnostic.pc,"terminal diagnostic retained");
    c.load(0x900,{0xa5}); auto oldrun=c.run(); c.reset();
    check(c.run()!=oldrun && c.boundary()=="0" && ps(c).time==0 && ps(c).frames.empty() && c.peek(0x900)==0xa5 && !c.terminal_diagnostic(),"reset clears protected state and preserves RAM");
    (void)prior;
    const std::vector<std::pair<std::vector<uint8_t>,unsigned>> privileged={
        {{0x10,0x93},1},{{2},2},{{3},2},{{1},3},{{0x10,0x91},4},{{0x10,0xd3},4},
        {{0x1f,0x20,0},5},{{0x1f,0x80,0x20},5},{{0x1f,0x40,0},6},{{0x1f,0x90,0x20},6},
        {{0x1f,0x12},7},{{0x1f,0xa0,0},8},{{0x1f,0xb0,0x7f},9},{{0x27,0,0},10}};
    for (const auto& item:privileged) {
        auto u=machine(); user_map(u); user_return(u); u.load(0x10100,item.first); u.registers().r[0]=0xffff; u.step();
        fault(u,Cause::Privilege);
        check(ps(u).frames.back().privilege==item.second && ps(u).frames.back().origin==Mode::User && ((*u.context())[10]&32),"all ten U privilege classes, I preserved in capture");
    }
    for (auto bytes:{std::vector<uint8_t>{0x1f,0xa8},std::vector<uint8_t>{0x1f,0xa0,0x7f},std::vector<uint8_t>{0x27,3},std::vector<uint8_t>{0x27,0x40,4}}) {
        auto u=machine(); user_map(u); user_return(u); u.load(0x10100,bytes); u.step(); fault(u,Cause::Illegal);
        check(ps(u).frames.back().privilege==0,"reserved decode precedes privilege");
    }
    auto u=machine(); user_map(u); user_return(u); u.load(0x10100,{0x1f,0xa0,0}); u.map(0x10102,1,false); u.step(); fault(u,Cause::Memory);
    check(ps(u).frames.back().memory[1]==1 && ps(u).frames.back().privilege==0,"truncated legal fetch precedes privilege");
}
void user_alu_extensions() {
    auto u=machine(); user_map(u); user_return(u);
    for (unsigned tag=0x60;tag<=0x87;++tag) for (unsigned count=0;count<=16;++count) {
        unsigned reg=tag&3; u.registers().r[reg]=0xa581; u.registers().flags=0x2f;
        u.load(0x10100,{0x1a,uint8_t(tag),uint8_t(count)}); u.registers().pc=0x100; u.step();
        check(u.mode()==Mode::User && !u.context() && (u.csr()&0x28)==0x28,"all immediate shift/rotate counts execute in U preserving V/I");
        if (!count) check(u.registers().r[reg]==0xa581 && u.csr()==0x2f,"U zero-count preserves all state");
        else if (!(tag&4)) check(u.registers().r[reg]<256,"U positive byte shift zero-extends");
    }
    for (unsigned tag=0x40;tag<=0x4b;++tag) {
        unsigned reg=tag&3; u.registers().r[reg]=0xa581; u.registers().flags=0x2f;
        std::vector<uint8_t> bytes={0x1a,uint8_t(tag)};
        if (tag>=0x44) bytes.push_back(0x80); if (tag>=0x48) bytes.push_back(0);
        u.load(0x10100,bytes); u.registers().pc=0x100; u.step();
        check(u.mode()==Mode::User && !u.context() && u.registers().r[reg]==0xa581 && (u.csr()&0x2c)==0x2c,"U TST/TSTI preserve register/C/V/I");
    }
    for (unsigned op=0x80;op<=0xaf;++op) {
        unsigned reg=op&3; u.registers().r[reg]=0x12ff; u.registers().flags=32;
        u.load(0x10100,{uint8_t(op),1,0}); u.registers().pc=0x100; u.step();
        check(u.mode()==Mode::User && !u.context() && (u.csr()&32),"all immediate ALU opcodes execute in U");
    }
}
void user_access() {
    for (unsigned reason=1;reason<=8;++reason) {
        auto c=machine(); user_map(c); setc(c,1,7); setc(c,2,3); c.registers().r[4]=0x2000;
        if (reason==1) c.registers().r[4]=0xffff;
        if (reason==2) setc(c,1,0);
        if (reason==3) setc(c,1,8);
        if (reason==4) setc(c,2,4);
        if (reason==5) descriptor(c,7,3,{3,0x19,0,0x2000,0,0,0});
        if (reason==6) descriptor(c,7,3,{3,0x11,0,0,1,0,0});
        if (reason==7) { descriptor(c,7,3,{255,0x19,0,0,1,0,0}); c.registers().r[4]=0; }
        if (reason==8) c.map(0x32001,1,false);
        auto commits=ps(c).device_commits; auto original=c.registers().r[0]; execute(c,{0x27,0,0}); fault(c,Cause::Memory);
        auto m=ps(c).frames.back().memory;
        check(m[0]==3 && m[1]==2 && m[2]==2 && m[9]==2 && m[10]==reason,"MOVU exact origin/phase/access/width/reason "+std::to_string(reason)+" actual "+std::to_string(m[10]));
        check(c.registers().r[0]==original && ps(c).device_commits==commits,"failed MOVU no destination/device effect");
        if (reason==8) check(m[5]==0x2000 && m[6]==0 && m[7]==0x2001 && m[8]==0x103 && (*c.context())[18]==0x2001 && (*c.context())[19]==0x103,"MOVU missing second byte exact virtual/physical addresses");
    }
    auto c=machine(); user_map(c); setc(c,1,7); setc(c,2,3);
    for (unsigned store=0;store<2;++store) for (unsigned byte=0;byte<2;++byte) for (unsigned disp=0;disp<2;++disp)
        for (unsigned reg=0;reg<8;++reg) for (unsigned base=0;base<4;++base) {
            c.registers().r[reg]=0x1234; c.registers().r[4+base]=0x2000;
            auto original=c.registers().r[reg]; c.load(0x32000,{0xcd,0xab}); c.registers().flags=0xf;
            uint8_t x=(store?128:0)|(byte?32:0)|(disp?64:0)|(reg<<2);
            execute(c,disp?std::vector<uint8_t>{0x27,x,uint8_t(base),0,0}:std::vector<uint8_t>{0x27,x,uint8_t(base)});
            check(!c.context() && c.csr()==15,"MOVU all forms preserve flags");
            check(store?(c.peek(0x32000)==uint8_t(original) && (byte || word(c,0x32000)==original)):
                        c.registers().r[reg]==(byte?0xcd:0xabcd),"MOVU alias samples original base/source");
        }
    c.registers().r[4]=0x2000; c.registers().r[0]=0xbeef; c.load(0x32000,{0x55,0x66}); c.map(0x32001,1,false);
    execute(c,{0x27,0x80,0}); fault(c,Cause::Memory);
    check(c.peek(0x32000)==0x55 && c.peek(0x32001)==0x66 && ps(c).frames.back().memory[2]==3,"second-byte store failure atomic");
    c.map(0x32001,1,true); setx(c,8,0x700); execute(c,{0x1f,0x12},0x800); c.load(0x700,{0x27,0x80,0}); c.step();
    check(word(c,0x32000)==0xbeef && !c.context(),"repaired user-access transfer retry");
    auto u=machine(); user_map(u); user_return(u); u.load(0x10100,{0x1f,0,0}); u.registers().sp=0; u.step(); fault(u,Cause::Stack);
    check(ps(u).frames.back().origin==Mode::User && u.registers().sp==0xfffe,"bad user stack captures independently on kernel stack");
}
void terminal_entry() {
    for (unsigned failure=0;failure<5;++failure) {
        auto c=machine();
        if (failure==0) c.map(8,1,false);
        if (failure==1) c.load(11,{1});
        if (failure==2) vector_to(c,2,0x80100);
        if (failure==3) {
            setc(c,0x10,3); setc(c,0x7f,5);
            descriptor(c,0,3,{3,0x101,0,0,1,0,0});
        }
        if (failure==4) c.registers().sb=c.registers().sp;
        auto before=c.registers(); before.pc=0x700;
        c.registers().r[4]=0; c.registers().ds=9;
        execute(c,{0x15,0x80,0}); // Installed descriptor, absent backing: ordinary Memory fault.
        check(c.state()==ExecutionState::TerminalDoubleFault && ps(c).frames.size()==1,"failed entry retains captured frame");
        static const char* stages[]={"vector_read","vector_format","handler","DS","stack"};
        check(c.terminal_diagnostic()->stage==stages[failure] && c.terminal_diagnostic()->captured,"failed entry stage diagnostic "+std::to_string(failure)+" actual "+c.terminal_diagnostic()->stage);
        check(c.registers().pc==0x700 && c.registers().ds==9,"failed entry retains live pre-entry state");
        auto b=c.boundary(); auto t=ps(c).time; check(!c.step() && b==c.boundary() && t==ps(c).time,"terminal poll no B/T progress");
        (void)before;
    }
    VirtualCPU reset(Revision::Working,0x20000); reset.load(0,{0,1,0,1}); reset.reset();
    check(reset.state()==ExecutionState::TerminalResetFault && ps(reset).frames.empty() && reset.registers().pc==0,"invalid reset vector terminal without frame");
}
void timer_config(VirtualCPU& c,uint64_t d,uint64_t p,unsigned mode) {
    for (unsigned i=0;i<4;++i) mmio_write(c,0x30+2*i,uint16_t(d>>(16*i)));
    for (unsigned i=0;i<4;++i) mmio_write(c,0x38+2*i,uint16_t(p>>(16*i)));
    mmio_write(c,0x24,mode); mmio_write(c,0x20,2);
}
void devices_and_time() {
    auto c=machine();
    std::vector<unsigned> readable={0,2,4,8,10,0x22,0x24,0x26};
    for (unsigned o=0x30;o<=0x56;o+=2) readable.push_back(o);
    for (auto o:readable) {
        auto commits=ps(c).device_commits; mmio_read(c,o);
        check(!c.context() && ps(c).device_commits==commits+1,"assigned MMIO register one word callback");
    }
    check(mmio_read(c,10)==7,"connected PIC sources");
    for (unsigned bit=0;bit<16;++bit) { mmio_write(c,2,1u<<bit); check(ps(c).enable==(1u<<bit),"all PIC enable bits"); mmio_write(c,6,1u<<bit); }
    mmio_write(c,2,0xffff);
    auto a=event(c,BoundaryEvent::Kind::Edge,1),b=a; b.kind=BoundaryEvent::Kind::Level; b.source=2; b.asserted=true; b.sequence="1";
    check(c.apply_boundary_events({a,b})==HostError::None && ps(c).pending==6,"same-boundary edge and level");
    c.registers().flags=32; c.registers().pc=0x800; auto t=ps(c).time; c.step();
    check(ps(c).frames.back().source==1 && ps(c).in_service==2 && ps(c).pending==4 && ps(c).time==t,"PIC priority and IRQ B-only boundary");
    c.registers().flags=32; c.step();
    check(ps(c).frames.size()==2 && ps(c).frames.back().source==2 && ps(c).in_service==6,"IRQ nesting ignores E and excludes in-service");
    auto level=event(c,BoundaryEvent::Kind::Level,2); level.asserted=false;
    check(c.apply_boundary_events({level})==HostError::None && ps(c).levels==0,"level deassertion");
    mmio_write(c,6,4); check(ps(c).in_service==2,"ACK source independent of return");
    execute(c,{0x1f,0x12}); check(ps(c).frames.size()==1 && ps(c).in_service==2,"IRET does not ACK source");
    mmio_write(c,6,2); execute(c,{0x1f,0x12}); check(ps(c).frames.empty(),"nested IRQ unwind");
    // Assert/deassert at one boundary leaves a sticky pending bit.
    level=event(c,BoundaryEvent::Kind::Level,2); level.asserted=true; auto off=level; off.asserted=false;
    off.sequence=std::to_string(std::stoull(level.sequence)+1);
    c.registers().flags=0;
    check(c.apply_boundary_events({level,off})==HostError::None && ps(c).pending==4 && ps(c).levels==0,"deassert never withdraws pending");
    mmio_write(c,6,4); check(ps(c).pending==4,"ACK never clears pending");
    for (unsigned width:{1u,2u}) for (unsigned o=0;o<256;++o) {
        if (width==2 && std::find(readable.begin(),readable.end(),o)!=readable.end()) continue;
        auto d=machine(); auto commits=ps(d).device_commits; d.registers().ds=255;
        execute(d,{0x15,uint8_t(width==1?0x18:0x10),uint8_t(o),0}); fault(d,Cause::Memory);
        check(ps(d).device_commits==commits && ps(d).frames.back().memory[10]==(width==2 && o==255?8:7),"reserved/unaligned/byte MMIO denied before callback");
    }
    for (unsigned o=0;o<256;++o) {
        auto d=machine(); bool permitted=o==2 || o==6 || o==0x20 || o==0x24 || (!(o&1) && o>=0x30 && o<=0x3e);
        mmio_write(d,o,1);
        if (permitted) check(!d.context() && ps(d).device_commits==1,"all writable MMIO directions commit once");
        else { fault(d,Cause::Memory); check(ps(d).device_commits==0,"read-only/reserved MMIO writes have no callback"); }
    }
    auto d=machine(); d.registers().ds=255; d.registers().r[0]=0xffff;
    execute(d,{0x15,0x50,2,0},0xfffc); fault(d,Cause::Memory);
    check(ps(d).enable==0 && ps(d).device_commits==0 && ps(d).frames.back().memory[1]==3,"MMIO next-PC failure commits no callback");
    d=machine(); timer_config(d,100,0,1); auto old=ps(d).deadline;
    mmio_write(d,0x30,0); auto commits=ps(d).device_commits; mmio_write(d,0x20,2); fault(d,Cause::Memory);
    check(ps(d).deadline==old && ps(d).timer_mode==1 && ps(d).device_commits==commits,"failed timer commit retains active configuration");
    resume(d); auto before=ps(d).time; mmio_write(d,0x20,1);
    check(ps(d).snapshot_valid && ps(d).snapshot==before && ps(d).time==before+1,"atomic snapshot is pre-retirement time");
    check(mmio_read(d,0x40)==uint16_t(before),"snapshot word readback");
    d=machine();
    mmio_write(d,0x24,1); mmio_write(d,0x30,uint16_t(ps(d).time+2)); mmio_write(d,0x20,2);
    check(ps(d).pending==1 && ps(d).timer_mode==0 && ps(d).deadline==ps(d).time,"D=T+1 commit expires on own retirement");
    d=machine();
    uint64_t deadline=ps(d).time+20; timer_config(d,deadline,0,1);
    while (ps(d).time<deadline) execute(d,{0});
    check(ps(d).pending==1 && ps(d).timer_mode==0,"one-shot exact retirement deadline");
    d=machine(); timer_config(d,100,3,3); execute(d,{1}); auto boundary=d.boundary();
    check(d.advance_idle_to(110)==HostError::None && ps(d).deadline==112 && ps(d).pending==1,"periodic idle advancement preserves phase and coalesces");
    check(d.boundary()==boundary && !d.step(),"idle advance changes T, not B; HALT polls do not retire");
    d=machine(); timer_config(d,std::numeric_limits<uint64_t>::max()-2,4,3); execute(d,{1});
    check(d.advance_idle_to(std::numeric_limits<uint64_t>::max()-2)==HostError::None && ps(d).deadline_overflow && ps(d).timer_mode==0 && ps(d).pending==1,"checked periodic overflow disables without wrapping");
    check(d.advance_idle_to(std::numeric_limits<uint64_t>::max())==HostError::None && ps(d).time==UINT64_MAX,"time saturates at uint64 maximum");
    d=machine(); timer_config(d,100,0,1); mmio_write(d,2,1); d.registers().flags=32; execute(d,{1});
    check(d.advance_idle_to(101)==HostError::BadTime && d.advance_idle_to(100)==HostError::None,"idle cannot skip enabled wake deadline");
    check(d.advance_idle_to(101)==HostError::BadState,"eligible wake prevents idle advancement");
    t=ps(d).time; d.step(); check(d.state()==ExecutionState::Running && ps(d).frames.back().source==0 && ps(d).time==t,"timer wakes HALT without retirement");
    auto failed=machine(); mmio_write(failed,2,2); auto edge=event(failed,BoundaryEvent::Kind::Edge,1);
    failed.apply_boundary_events({edge}); failed.load(23,{1}); failed.registers().flags=32; failed.step();
    check(failed.state()==ExecutionState::TerminalDoubleFault && ps(failed).pending==2 && ps(failed).in_service==0,"failed IRQ entry preserves pending and in-service");
}
std::string json_event(const VirtualCPU& c,const std::string& fields) {
    return "{\"run\":\""+c.run()+"\",\"boundary\":\""+c.boundary()+"\",\"sequence\":\""+c.next_sequence()+"\","+fields+"}";
}
void replay() {
    auto c=machine(); auto before=ps(c); auto sequence=c.next_sequence(); auto boundary=c.boundary();
    auto a=event(c,BoundaryEvent::Kind::Edge,1),b=a; b.sequence="99";
    check(c.apply_boundary_events({a,b})==HostError::BadSequence && ps(c).pending==before.pending && c.next_sequence()==sequence,"event batch rollback");
    b=a; b.run="999999999999999999999999999999999999999999";
    check(c.apply_boundary_events({b})==HostError::WrongBoundary,"arbitrary decimal run mismatch without truncation");
    for (auto text:{"", "00", "+1", "-1", "1.0", " 1"}) {
        b=a; b.sequence=text; check(c.apply_boundary_events({b})==HostError::BadFormat,"canonical decimal ordinal required");
    }
    for (unsigned source:{0u,2u,3u,15u,65535u}) { b=a; b.source=source; check(c.apply_boundary_events({b})==HostError::BadSource,"external timer and disconnected source rejection"); }
    std::vector<std::string> invalid={"", "null", "[]", "{}", "{", "{\"run\":\"0\",\"run\":\"0\"}",
        json_event(c,"\"kind\":\"irq_edge\",\"source\":1,\"extra\":0"),
        json_event(c,"\"kind\":\"irq_edge\",\"source\":1.0"),
        json_event(c,"\"kind\":\"irq_level\",\"source\":2,\"asserted\":1"),
        json_event(c,"\"kind\":\"irq_edge\",\"source\":1,\"sour\\u0063e\":1"),
        json_event(c,"\"kind\":\"\\uD800\",\"source\":1"),
        json_event(c,std::string("\"kind\":\"")+char(0xc0)+char(0x80)+"\",\"source\":1")};
    for (const auto& line:invalid) {
        if (line.empty()) continue; // Empty batch is a valid no-op.
        check(c.apply_boundary_json(line)==HostError::BadFormat && ps(c).pending==before.pending && c.next_sequence()==sequence,"strict JSON format and atomic rejection");
    }
    check(c.apply_boundary_json("")==HostError::None && c.boundary()==boundary,"empty event batch no-op");
    auto wrong=json_event(c,"\"kind\":\"irq_edge\",\"source\":0");
    check(c.apply_boundary_json(wrong+"\n{bad")==HostError::BadSource,"first record semantic error precedes later parse error");
    auto valid=json_event(c,"\"kind\":\"irq_edge\",\"source\":1");
    check(c.apply_boundary_json(valid+"\n{bad")==HostError::BadFormat && ps(c).pending==0,"later parse failure rolls back earlier edge");
    check(c.apply_boundary_json(valid)==HostError::None && ps(c).pending==2 && c.next_sequence()=="1","valid JSON edge");
    auto reset=event(c,BoundaryEvent::Kind::Reset); auto oldrun=c.run();
    check(c.apply_boundary_events({reset})==HostError::None && c.run()!=oldrun && c.boundary()=="0" && c.next_sequence()=="0" && ps(c).pending==0,"recorded reset starts fresh run");
    check(c.apply_boundary_events({a})==HostError::WrongBoundary,"old-run queued events rejected");
    // SHA-256 constants independently generated by Python hashlib over the exact raw artifacts.
    VirtualCPU artifact(Revision::Working,0x20000); artifact.load(0,{0,1,0,0}); artifact.load(0x100,{1}); artifact.reset();
    std::vector<uint8_t> ram(1u<<24),backing(1u<<24); ram[1]=1; ram[0x100]=1;
    std::fill(backing.begin(),backing.begin()+0x20000,1); std::fill(backing.begin()+0xff0000,backing.begin()+0xff0100,2);
    std::string header="{\"format\":\"vcpu-replay\",\"version\":1,\"profile\":\"minimal-v1\",\"run\":\"0\","
        "\"ram_image_sha256\":\"4bf9f807b3a30d1c0f9406c25ffeca0d19700c1194d0ac0fe67f83c9bb8cb03c\","
        "\"backing_map_sha256\":\"5a1c4e879245e3c89516c2e251fe9372e99ed48cc675c10349361bd613bda940\"}";
    check(artifact.verify_replay_header(header,ram,backing)==HostError::None,"exact replay artifacts and independent SHA-256");
    ram[0x102]=1; check(artifact.verify_replay_header(header,ram,backing)==HostError::BadProfile,"artifact byte mismatch"); ram[0x102]=0;
    auto badheader=header; badheader[badheader.find("4bf9")]='0';
    check(artifact.verify_replay_header(badheader,ram,backing)==HostError::BadProfile,"artifact hash mismatch");
    artifact.step(); check(artifact.verify_replay_header(header,ram,backing)==HostError::WrongBoundary,"header only accepted at reset boundary");
    // Identical recorded input must give identical architectural/device state at every boundary.
    auto left=machine(),right=machine(); mmio_write(left,2,6); mmio_write(right,2,6);
    for (unsigned i=0;i<200;++i) {
        if (i%7==0) {
            auto l=event(left,BoundaryEvent::Kind::Edge,1),r=event(right,BoundaryEvent::Kind::Edge,1);
            check(left.apply_boundary_events({l})==right.apply_boundary_events({r}),"replay event result deterministic");
        }
        // This stream keeps I=0: edges coalesce without hidden wall-clock progress.
        execute(left,{0x80,1}); execute(right,{0x80,1});
        check(left.registers().r==right.registers().r && left.csr()==right.csr() && left.boundary()==right.boundary() &&
          left.next_sequence()==right.next_sequence() && ps(left).pending==ps(right).pending && ps(left).time==ps(right).time &&
          ps(left).descriptors==ps(right).descriptors && ps(left).frames.size()==ps(right).frames.size(),"boundary replay state identical");
    }
}
void kernel_demo() {
    VirtualCPU c(Revision::Working,0x80000);
    for (unsigned i=0;i<8;++i) vector_to(c,i,i==0?0x100:i==5?0x1000:i==6?0x1200:i==2?0x1300:0x1400);
    using namespace kernel_images;
    c.load(boot_origin,boot); c.load(irq_origin,irq); c.load(swi_origin,swi);
    c.load(memory_origin,memory); c.load(copy_origin,copy); c.load(load_context_origin,load_context);
    c.load(0x1400,{1}); c.load(0x10100,user); c.load(0x20100,user); c.reset();
    auto replayed=c;
    bool seven=false,eight=false,nested=false; size_t count=0;
    while (c.state()==ExecutionState::Running && count++<20000) {
        c.step(); replayed.step();
        auto& p=ps(c); auto& q=ps(replayed); auto& a=c.registers(); auto& b=replayed.registers();
        bool same=c.state()==replayed.state() && c.boundary()==replayed.boundary() && c.context()==replayed.context() &&
            a.r==b.r && a.pc==b.pc && a.cs==b.cs && a.ds==b.ds && a.ss==b.ss && a.sp==b.sp && a.sb==b.sb && a.st==b.st && a.dp==b.dp && c.csr()==replayed.csr() &&
            p.mode==q.mode && p.controls==q.controls && p.published==q.published && p.descriptors==q.descriptors &&
            p.shadow==q.shadow && p.entry==q.entry && p.entry_shadow==q.entry_shadow && p.entry_configured==q.entry_configured &&
            p.time==q.time && p.deadline==q.deadline && p.period==q.period && p.timer_mode==q.timer_mode && p.shadow_deadline==q.shadow_deadline &&
            p.shadow_period==q.shadow_period && p.shadow_mode==q.shadow_mode && p.pending==q.pending && p.enable==q.enable &&
            p.in_service==q.in_service && p.levels==q.levels && p.frames.size()==q.frames.size() && p.device_commits==q.device_commits;
        for (size_t i=0;i<p.frames.size() && same;++i) {
            const auto& f=p.frames[i]; const auto& g=q.frames[i];
            same=f.words==g.words && f.controls==g.controls && f.origin==g.origin && f.bootstrap==g.bootstrap &&
                 f.prior_depth==g.prior_depth && f.source==g.source && f.privilege==g.privilege && f.memory==g.memory;
        }
        check(same,"guest kernel replay matches at every boundary");
        seven|=c.mode()==Mode::User && ps(c).controls[0]==7;
        eight|=c.mode()==Mode::User && ps(c).controls[0]==8;
        nested|=ps(c).frames.size()==2 && ps(c).frames.back().memory[0]==3;
    }
    if (c.state()!=ExecutionState::Halted || c.registers().pc!=swi_origin+swi.size()) {
        std::ostringstream message; message<<"kernel demo failed at "<<std::hex<<c.registers().cs<<":"<<c.registers().pc
          <<" state="<<int(c.state())<<" steps="<<std::dec<<count<<" depth="<<ps(c).frames.size();
        if (c.context()) message<<" cause="<<(*c.context())[17]<<" savedPC="<<std::hex<<(*c.context())[8];
        if (c.terminal_diagnostic()) message<<" stage="<<c.terminal_diagnostic()->stage;
        throw std::runtime_error(message.str());
    }
    check(seven && eight && nested,"guest boot, two maps, nested user-copy fixup");
    check(word(c,0x3002)==1 && word(c,0x3004)==1 && word(c,0x3006)>=4,"guest timer scheduling progress");
    check(word(c,0x32000)>=3 && word(c,0x42000)>=3 && word(c,0x3008)==2,"isolated user counters and copy errors");
    check(ps(c).enable==0 && ps(c).timer_mode==0 && !c.terminal_diagnostic(),"clean guest shutdown");
    bool same_ram=true; for (unsigned a=0;a<0x80000;++a) same_ram&=c.peek(a)==replayed.peek(a);
    check(same_ram,"guest kernel replay final RAM identical");
    std::cout<<"guest kernel: "<<count<<" boundaries, "<<word(c,0x3006)<<" timer switches\n";
}
}
int main() {
    try { immediate_alu(); controls_and_maps(); protection_boundaries(); context_and_privilege(); user_alu_extensions(); user_access(); terminal_entry(); devices_and_time(); replay(); kernel_demo(); std::cout<<checks<<" protected checks passed\n"; }
    catch (const std::exception& e) { std::cerr<<e.what()<<'\n'; return 1; }
}
