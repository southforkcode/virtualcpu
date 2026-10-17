#include "cpu.hpp"
#include <iostream>
#include <stdexcept>
using namespace vcpu;
namespace {
size_t checks=0;
void check(bool ok,const char* message) {++checks;if(!ok)throw std::runtime_error(message);}
void setc(VirtualCPU& c,unsigned selector,unsigned value) {
    auto old=c.registers();c.load(0x700,{0x1f,0xb7,uint8_t(selector)});c.registers().pc=0x700;c.registers().r[7]=value;c.step();c.registers()=old;
}
VirtualCPU machine() {
    VirtualCPU c(0x40000);
    for(unsigned v=0;v<8;++v)c.load(4*v,{0,uint8_t(v?3:1),0,0});
    c.load(0x300,{0x1f,0x12});c.reset();
     {for(unsigned i=0;i<5;++i)setc(c,0x10+i,i<3?0:0xfffe);setc(c,0x7f,5);}
    return c;
}
void run(VirtualCPU& c,std::vector<uint8_t> bytes,unsigned pc=0x100) {
    c.load(pc,bytes);c.registers().pc=pc;c.step();
}
void fault(const VirtualCPU& c,Cause cause,unsigned pc) {
    check(c.context() && (*c.context())[17]==unsigned(cause) && (*c.context())[8]==pc,"branch precise cause and fault PC");
    check(c.state()==ExecutionState::Running,"branch exception entry");
}
bool taken(unsigned condition,unsigned flags) {
    bool z=flags&1,n=flags&2,carry=flags&4,overflow=flags&8;
    switch(condition) {
    case 0:return z;case 1:return !z;case 2:return carry;case 3:return !carry;
    case 4:return n;case 5:return !n;case 6:return overflow;case 7:return !overflow;
    case 8:return carry&&!z;case 9:return !carry||z;case 10:return n==overflow;
    case 11:return n!=overflow;case 12:return !z&&n==overflow;case 13:return z||n!=overflow;default:return true;
    }
}
void predicates() {
    auto c=machine();
    for(unsigned condition=0;condition<15;++condition)for(unsigned flags=0;flags<16;++flags)
    for(unsigned interrupt:{0u,32u})for(int displacement=-128;displacement<=127;++displacement) {
        auto& s=c.registers();s.r.fill(0x9876);s.flags=flags|interrupt;s.ds=254;s.dp=0x4567;
        auto old=s;auto time=c.protected_state()->time;
        run(c,{uint8_t(0x60+condition),uint8_t(displacement)},0x1000);
        check(!c.context() && s.pc==(taken(condition,flags)?0x1002+displacement:0x1002),"rel8 exhaustive predicate/displacement oracle");
        check(s.r==old.r && s.flags==old.flags && s.ds==old.ds && s.dp==old.dp && s.sp==old.sp && s.ss==old.ss,"short branch preserves registers/flags and ignores DS");
        check(c.protected_state()->time==time+1,"short branch retires one deterministic tick");
    }
    {
        auto fixture=machine();
        for(unsigned condition=0;condition<15;++condition)for(unsigned flags=0;flags<16;++flags)
        for(int displacement:{-128,-1,0,1,127}) {
            auto long_form=fixture;long_form.registers().flags=flags;
            run(long_form,{uint8_t(0x40+condition),uint8_t(displacement),uint8_t(uint16_t(displacement)>>8)},0x1000);
            check(!long_form.context() && long_form.registers().flags==flags && long_form.registers().pc==(taken(condition,flags)?0x1003+displacement:0x1003),"rel16 behavior long-form behavior");
        }
    }
}
void decoding() {
    {
        auto fixture=machine();
        for(unsigned op=0x60;op<=0x6f;++op) {
            auto c=fixture;c.map(0x101,1,false);run(c,{uint8_t(op)});
            fault(c,op!=0x6f?Cause::Memory:Cause::Illegal,0x100);
            if(op!=0x6f)check(c.protected_state()->frames.back().memory[1]==1,"legal rel8 waits for complete displacement fetch");
        }
        auto c=fixture;run(c,{0x6f},0xffff);fault(c,Cause::Illegal,0xffff);
    }
    auto c=machine();c.map(0x102,1,false);run(c,{0x6e,0xfe});
    check(!c.context() && c.registers().pc==0x100,"rel8 does not fetch a third byte");
    c=machine();run(c,{0x6e},0xffff);fault(c,Cause::Memory,0xffff);
    check(c.protected_state()->frames.back().memory[1]==1 && c.protected_state()->frames.back().memory[10]==1,"rel8 fetch cannot wrap segments");
}
void segment_ends() {
    auto fixture=machine();
    for(unsigned condition=0;condition<15;++condition)for(unsigned flags=0;flags<16;++flags)
    for(int displacement:{-128,-2,-1,0,1,127}) {
        auto c=fixture;c.registers().flags=flags;c.registers().r.fill(0xabcd);auto time=c.protected_state()->time;
        int target=taken(condition,flags)?65536+displacement:65536;
        run(c,{uint8_t(0x60+condition),uint8_t(displacement)},0xfffe);
        if(target<65536)check(!c.context() && c.registers().pc==target && c.registers().flags==flags,"taken rel8 widened next at segment end");
        else {
            fault(c,Cause::Memory,0xfffe);const auto& f=c.protected_state()->frames.back();
            check(f.memory[1]==3 && f.memory[10]==1 && (*c.context())[10]==flags,"untaken/overflow rel8 selected-PC failure");
            check(c.protected_state()->time==time && c.registers().r[0]==0xabcd,"failed branch does not retire or change registers");
        }
    }
    for(int displacement:{-128,-3}) {
        auto c=fixture;run(c,{0x6e,uint8_t(displacement)},0);fault(c,Cause::Memory,0);
        uint32_t wide=uint32_t(2+displacement);const auto& m=c.protected_state()->frames.back().memory;
        check(m[1]==3 && m[5]==uint16_t(wide) && m[6]==uint16_t(wide>>16),"short branch target underflow retains widened metadata");
    }
    auto c=fixture;run(c,{0x60,0},0xfffd);check(!c.context() && c.registers().pc==0xffff,"rel8 last representable fallthrough");
}
void targets_and_frames() {
    for(bool branch:{false,true}) {
        auto c=machine();c.map(0x110,1,false);c.registers().flags=branch?1:0;
        auto old=c.registers();auto time=c.protected_state()->time;run(c,{0x60,14});
        if(branch) {
            fault(c,Cause::Memory,0x100);const auto& m=c.protected_state()->frames.back().memory;
            check(m[1]==4 && m[10]==8 && m[5]==0x110 && (*c.context())[10]==old.flags,"taken rel8 validates target backing at CONTROL_TARGET");
            check(c.registers().r==old.r && c.protected_state()->time==time,"target fault atomicity");
        } else check(!c.context() && c.registers().pc==0x102,"untaken branch never probes discarded target");
    }
    auto c=machine();run(c,{0x26,0});auto frame=*c.context();
    for(unsigned condition=0;condition<15;++condition) {
        c.registers().flags=0x2f;run(c,{uint8_t(0x60+condition),3},0x300);
        check(c.context() && *c.context()==frame && c.csr()==0x6f && c.protected_state()->frames.size()==1,"rel8 preserves active E/frame/NZCVI");
    }
    c.registers().flags=0;run(c,{0x60,0},0xfffe);fault(c,Cause::Memory,0xfffe);
    check(c.protected_state()->frames.size()==2 && c.protected_state()->frames.front().words==frame,"failed rel8 retains lower exception frame");
    // A U branch may execute from an execute-only segment, but must not jump
    // past its executable bound; exception entry remains independent of U SS.
    c=machine();
    auto descriptor=[&](unsigned seg,std::array<uint16_t,7> d) {
        setc(c,0x20,7);setc(c,0x21,seg);for(unsigned i=0;i<7;++i)setc(c,0x22+i,d[i]);setc(c,0x7f,1);
    };
    descriptor(1,{1,0x21,0x100,0x104,0,0,0});descriptor(2,{2,0x1d,0x1000,0x2000,0,0x1000,0x2000});
    descriptor(3,{3,0x19,0,0,1,0,0});setc(c,0x7f,3);c.load(0x10100,{0x6e,6});run(c,{0x26,0});
    auto setx=[&](unsigned selector,unsigned value) {
        c.registers().r[7]=value;
        run(c,selector<0x20?std::vector<uint8_t>{0x1f,uint8_t(0x40+selector),7}:std::vector<uint8_t>{0x1f,0x97,uint8_t(selector)},0x700);
    };
    for(auto p:{std::pair<unsigned,unsigned>{8,0x100},{9,1},{10,32},{11,0x2000},{12,0x1000},{13,0x2000},{14,2},{16,3},{0x20,7},{0x21,0},{0x22,0}})setx(p.first,p.second);
    setc(c,0x7f,7);run(c,{0x1f,0x12},0x700);check(c.mode()==Mode::User && !c.context(),"rel8 U fixture");c.step();fault(c,Cause::Memory,0x100);
    auto m=c.protected_state()->frames.back().memory;
    check(m[0]==1 && m[1]==4 && m[3]==7 && m[4]==1 && m[5]==0x108 && m[10]==5,"U rel8 target requires executable descriptor interval");
}
}
int main() {
    try {predicates();decoding();segment_ends();targets_and_frames();std::cout<<checks<<" short branch checks passed\n";}
    catch(const std::exception& e) {std::cerr<<"short branch: "<<e.what()<<'\n';return 1;}
}
