#include "cpu.hpp"
#include <iostream>
#include <stdexcept>
using namespace vcpu;
namespace {
size_t checks=0;
void check(bool ok,const char* message) { ++checks; if (!ok) throw std::runtime_error(message); }
void setc(VirtualCPU& c,uint8_t selector,uint16_t value) {
    auto old=c.registers(); c.load(0x700,{0x1f,0xb7,selector});
    c.registers().pc=0x700; c.registers().r[7]=value; c.step(); c.registers()=old;
}
void descriptor(VirtualCPU& c,unsigned seg,std::array<uint16_t,7> d,unsigned map=0) {
    setc(c,0x20,map); setc(c,0x21,seg);
    for (unsigned i=0;i<7;++i) setc(c,0x22+i,d[i]);
    setc(c,0x7f,1); check(!c.context(),"descriptor fixture");
}
VirtualCPU machine() {
    VirtualCPU c(0x40000,MachineProfile::ConsoleV1);
    for (unsigned v=0;v<exception_vector_count;++v) c.load(v*4,{0,uint8_t(v?3:1),0,0});
    c.load(0x300,{0x1f,0x12}); c.reset();
    {
        for (unsigned i=0;i<5;++i) setc(c,0x10+i,i<3?0:0xfffe);
        setc(c,0x7f,5);
    }
    return c;
}
std::vector<uint8_t> code(unsigned tag,unsigned base,int displacement,bool long_form=false) {
    if (long_form) return {0x15,uint8_t(tag|16),uint8_t(base),uint8_t(displacement),uint8_t(uint16_t(displacement)>>8)};
    return {0x15,uint8_t(tag),uint8_t(((unsigned(displacement)&63)<<2)|base)};
}
void run(VirtualCPU& c,const std::vector<uint8_t>& b,unsigned pc=0x100) {
    c.load(pc,b); c.registers().pc=pc; c.step();
}
void fault(const VirtualCPU& c,Cause cause,unsigned pc=0x100) {
    check(c.context() && (*c.context())[17]==unsigned(cause) && (*c.context())[8]==pc,"precise B1 cause/PC");
    check(c.state()==ExecutionState::Running,"B1 exception entry remains valid");
}
void exhaustive() {
    auto c=machine();
    // Independent encoding/address oracle: all 16,384 short forms, including
    // B1 source/destination aliases, old base sampling and both segments.
    for (unsigned cs=0;cs<2;++cs) for (unsigned store=0;store<2;++store)
    for (unsigned byte=0;byte<2;++byte) for (unsigned reg=0;reg<8;++reg)
    for (unsigned base=0;base<4;++base) for (int disp=-32;disp<=31;++disp) {
        auto& s=c.registers(); s.r.fill(0x4567); s.r[base+4]=0x2040;
        s.ds=1; s.flags=0x2f; auto old=s;
        unsigned target=(cs?0:0x10000)+0x2040+disp;
        c.load(target,{0x80,0x93});
        run(c,code(0x80|(cs<<5)|(store<<6)|(byte<<3)|reg,base,disp));
        check(!c.context() && s.pc==0x103 && s.flags==old.flags,"short B1 length/flags");
        if (store) {
            check(s.r==old.r,"B1 store samples old register bank");
            check(c.peek(target)==uint8_t(old.r[reg]) && c.peek(target+1)==(byte?0x93:uint8_t(old.r[reg]>>8)),"B1 store width/old alias source");
        } else {
            old.r[reg]=byte?0x80:0x9380;
            check(s.r==old.r,"B1 load zero extension/old alias base");
            check(c.peek(target)==0x80 && c.peek(target+1)==0x93,"B1 load preserves memory");
        }
    }

}
void long_equivalence() {
    auto fixture=machine();
    for (unsigned cs=0;cs<2;++cs) for (unsigned store=0;store<2;++store)
    for (unsigned byte=0;byte<2;++byte) for (unsigned reg=0;reg<8;++reg)
    for (unsigned base=0;base<4;++base) for (int disp:{-33,-32,-1,0,1,31,32}) {
        auto c=fixture; auto& s=c.registers();s.ds=1;s.flags=0x2f;s.r.fill(0x9876);s.r[base+4]=0x2040;
        auto old=s;unsigned target=(cs?0:0x10000)+0x2040+disp;c.load(target,{0x12,0x80});
        auto compact=c;unsigned tag=0x80|(cs<<5)|(store<<6)|(byte<<3)|reg;
        run(c,code(tag,base,disp,true));
        check(!c.context() && s.pc==0x105 && s.flags==old.flags,"working long B1 endpoints and noncanonical forms");
        if (store) check(s.r==old.r && c.peek(target)==uint8_t(old.r[reg]) && c.peek(target+1)==(byte?0x80:uint8_t(old.r[reg]>>8)),"long B1 store alias oracle");
        else { old.r[reg]=byte?0x12:0x8012;check(s.r==old.r,"long B1 load alias oracle"); }
        if (-32<=disp && disp<=31) {
            run(compact,code(tag,base,disp));
            check(compact.registers().r==s.r && compact.csr()==c.csr() && compact.peek(target)==c.peek(target) && compact.peek(target+1)==c.peek(target+1),"short/long B1 observable transfer equivalence");
        }
    }
}
void fetch_and_boundaries() {
    {
        auto fixture=machine();
        for (unsigned tag=0x90;tag<=0xff;++tag) {
            if (!(tag&16)) continue;
            for (unsigned d=4;d<256;++d) {
                auto c=fixture; c.map(0x103,2,false); run(c,{0x15,uint8_t(tag),uint8_t(d)});
                fault(c,Cause::Illegal);
            }
        }
    }
    for (bool store:{false,true}) for (bool longer:{false,true}) {
        auto b=code(store?0xc0:0x80,0,longer?32:2,longer);
        for (unsigned n=0;n<b.size();++n) {
            auto c=machine(); c.registers().r[4]=0x2000; c.registers().r[0]=0xabcd;
            c.load(0x100,b); c.map(0x100+n,1,false); auto old=c.registers(); c.step(); fault(c,Cause::Memory);
            check(c.protected_state()->frames.back().memory[1]==unsigned(Phase::Fetch),"truncated B1 fetch phase");
            check(c.peek(0x2000+(longer?32:2))==0 && c.registers().r==old.r,"truncated B1 store is atomic");
        }
        // Full fetch fits, result PC outside segment: stores/loads never commit.
        auto c=machine(); c.registers().r[4]=0x2000; c.registers().r[0]=0xabcd;
        unsigned pc=65536-b.size(); run(c,b,pc); fault(c,Cause::Memory,pc);
        check(c.protected_state()->frames.back().memory[1]==unsigned(Phase::ResultPC),"B1 final-PC failure");
        check(c.peek(0x2000+(longer?32:2))==0 && c.registers().r[0]==0xabcd,"B1 final-PC load/store atomicity");
        c=machine(); c.registers().r[4]=0x2000; pc=65535-b.size(); run(c,b,pc);
        check(!c.context() && c.registers().pc==65535,"B1 last valid selected-end PC");
        for (unsigned missing=1;missing<b.size();++missing) {
            c=machine(); pc=65536-missing;
            c.load(pc,std::vector<uint8_t>(b.begin(),b.begin()+missing)); c.registers().pc=pc; c.step();
            fault(c,Cause::Memory,pc); check(c.protected_state()->frames.back().memory[1]==1,"B1 fetch never wraps at segment end");
        }
    }
    // MOVU still reserves all six high descriptor bits, even in working mode.
    for (unsigned d=4;d<256;++d) {
        auto c=machine(); c.map(0x103,2,false); run(c,{0x27,0x40,uint8_t(d)}); fault(c,Cause::Illegal);
    }
}
void protection() {
    for (bool store:{false,true}) for (bool byte:{false,true})
    for (auto pair:{std::pair<unsigned,int>{0,-32},{0,-1},{65535,1},{65535,31}}) {
        auto c=machine(); c.registers().r[4]=pair.first; c.registers().r[0]=0xa5a5; auto old=c.registers();
        run(c,code(0x80|(store?64:0)|(byte?8:0),0,pair.second)); fault(c,Cause::Memory);
        const auto& f=c.protected_state()->frames.back(); uint32_t wide=uint32_t(int64_t(pair.first)+pair.second);
        check(f.memory[10]==1 && f.memory[5]==uint16_t(wide) && f.memory[6]==uint16_t(wide>>16),"B1 widened underflow/overflow metadata");
        check(c.registers().r==old.r && (*c.context())[10]==old.flags,"B1 range failure atomicity");
    }
    for (unsigned reason:{5u,6u,7u,8u}) for (bool store:{false,true}) {
        auto c=machine();
        descriptor(c,1,{1,uint16_t(reason==6?(store?0x41:0x81):reason==7?0xc3:0xc1),0,uint16_t(reason==5?0x2021:0),uint16_t(reason==5?0:1),0,0});
        c.registers().ds=1; c.registers().r[4]=0x2020; c.registers().r[0]=0xabcd;
        c.load(0x12020,{0x34,0x12}); if (reason==8) c.map(0x12021,1,false);
        run(c,code(0x80|(store?64:0),0,0)); fault(c,Cause::Memory);
        check(c.protected_state()->frames.back().memory[10]==reason,"B1 bounds/rights/kind/backing denial priority");
        check(c.peek(0x12020)==0x34 && c.registers().r[0]==0xabcd,"B1 denied word transfer has no partial effect");
    }
    // A word may straddle $FFFF despite a representable starting offset.
    auto c=machine(); c.registers().r[4]=65534;
    run(c,code(0xc0,0,1)); fault(c,Cause::Memory);
    check(c.protected_state()->frames.back().memory[10]==1,"B1 complete-width range validation");
    // Operand denial outranks a simultaneously invalid result PC.
    c=machine(); c.registers().r[4]=0; run(c,code(0xc0,0,-1),65533); fault(c,Cause::Memory,65533);
    check(c.protected_state()->frames.back().memory[1]==2,"B1 operand-before-result-PC priority");
}
void user_permissions() {
    for (unsigned scenario=0;scenario<3;++scenario) {
        auto c=machine();
        descriptor(c,1,{1,0x21,0x100,0x200,0,0,0},7);
        descriptor(c,2,{2,0x1d,0x1000,0x2000,0,0x1000,0x2000},7);
        descriptor(c,3,{3,0x09,0,0,1,0,0},7);setc(c,0x7f,3);
        c.registers().r[4]=scenario==2?0x110:0x2000;c.registers().r[0]=0xabcd;
        c.load(0x32002,{0x80,0x93});c.load(0x10112,{0x80,0x93});
        c.load(0x10100,code(scenario==2?0xa0:scenario==1?0xc0:0x80,0,2));
        run(c,{0x26,0});
        auto setx=[&](unsigned id,unsigned value) {
            c.registers().r[7]=value;
            run(c,id<0x20?std::vector<uint8_t>{0x1f,uint8_t(0x40+id),7}:std::vector<uint8_t>{0x1f,0x97,uint8_t(id)},0x700);
        };
        for (auto p:{std::pair<unsigned,unsigned>{8,0x100},{9,1},{10,32},{11,0x2000},{12,0x1000},{13,0x2000},{14,2},{16,3},{0x20,7},{0x21,0},{0x22,0}}) setx(p.first,p.second);
        setc(c,0x7f,7);run(c,{0x1f,0x12},0x700);
        check(c.mode()==Mode::User && !c.context(),"B1 U bootstrap fixture");c.step();
        if (scenario==0) check(!c.context() && c.registers().r[0]==0x9380 && c.registers().pc==0x103,"compact B1 U read-only data load");
        else {
            fault(c,Cause::Memory);
            const auto& f=c.protected_state()->frames.back();
            check(f.memory[10]==6 && f.memory[0]==1 && f.memory[3]==7 && f.memory[4]==(scenario==2?1:3),"compact B1 U write/execute-only read denial");
            check(c.peek(0x32002)==0x80 && c.peek(0x32003)==0x93 && c.registers().r[0]==0xabcd,"compact B1 U protection atomicity");
        }
    }
}
void mmio() {
    for (unsigned offset:{0x80u,0x82u}) for (unsigned scenario=0;scenario<5;++scenario) {
        auto c=machine();
        BoundaryEvent e{c.run(),c.boundary(),c.next_sequence(),BoundaryEvent::Kind::ConsoleInput}; e.data={0x41,0x42};
        check(c.apply_boundary_events({e})==HostError::None,"B1 MMIO input fixture");
        c.registers().ds=255; c.registers().r[4]=offset-2; c.registers().r[0]=0x43;
        bool store=offset==0x82, byte=scenario==1; unsigned tag=0x80|(store?64:0)|(byte?8:0);
        int disp=scenario==2?3:2; unsigned pc=scenario==3?65533:0x100;
        if (scenario==4) tag^=64; // Wrong device direction.
        auto commits=c.protected_state()->device_commits;
        run(c,code(tag,0,disp),pc);
        if (scenario) {
            fault(c,Cause::Memory,pc);
            check(c.protected_state()->console_rx.size()==2 && c.protected_state()->console_tx.empty() && c.protected_state()->device_commits==commits,"failed B1 MMIO neither consumes nor emits");
            check(c.protected_state()->frames.back().memory[1]==(scenario==3?3:2),"B1 MMIO fault phase");
        } else {
            check(!c.context() && c.protected_state()->device_commits==commits+1,"successful B1 MMIO exactly one commit");
            check(store?c.protected_state()->console_tx==std::vector<uint8_t>{0x43}:c.registers().r[0]==0x41 && c.protected_state()->console_rx==std::vector<uint8_t>{0x42},"B1 MMIO compact +2 semantics");
        }
    }
}
}
int main() {
    try { exhaustive(); long_equivalence(); fetch_and_boundaries(); protection(); user_permissions(); mmio(); std::cout<<checks<<" compact B1 checks passed\n"; }
    catch (const std::exception& e) { std::cerr<<"compact B1: "<<e.what()<<'\n'; return 1; }
}
