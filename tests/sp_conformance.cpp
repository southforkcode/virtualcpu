#include "cpu.hpp"
#include <iostream>
#include <stdexcept>
using namespace vcpu;
namespace {
size_t checks=0;
void check(bool ok,const char* text) {++checks;if(!ok)throw std::runtime_error(text);}
void setc(VirtualCPU& c,unsigned id,unsigned value) {
    auto old=c.registers();c.load(0x700,{0x1f,0xb7,uint8_t(id)});c.registers().pc=0x700;c.registers().r[7]=value;c.step();c.registers()=old;
}
void descriptor(VirtualCPU& c,unsigned map,unsigned seg,std::array<uint16_t,7> d) {
    setc(c,0x20,map);setc(c,0x21,seg);for(unsigned i=0;i<7;++i)setc(c,0x22+i,d[i]);setc(c,0x7f,1);
    check(!c.context(),"SP descriptor fixture");
}
VirtualCPU machine() {
    VirtualCPU c(0x40000,MachineProfile::ConsoleV1);
    for(unsigned n=0;n<vcpu::exception_vector_count;++n)c.load(4*n,{0,uint8_t(n?3:1),0,0});c.load(0x300,{0x1f,0x12});c.reset();
    {for(unsigned i=0;i<5;++i)setc(c,0x10+i,i<3?0:0xfffe);setc(c,0x7f,5);}
    return c;
}
void run(VirtualCPU& c,const std::vector<uint8_t>& b,unsigned pc=0x100) {
    c.load((c.mode()==Mode::User?0x10000:0)+pc,b);c.registers().pc=pc;c.step();
}
void fault(const VirtualCPU& c,Cause cause,unsigned pc=0x100) {
    check(c.context() && (*c.context())[17]==unsigned(cause) && (*c.context())[8]==pc,"SP precise cause/instruction PC");
    check(c.state()==ExecutionState::Running,"SP independent exception entry");
}
std::vector<uint8_t> code(unsigned tag,unsigned offset,bool longer=false) {
    if(longer)return {0x15,uint8_t(tag),uint8_t(offset),uint8_t(offset>>8)};
    return {0x28,uint8_t(tag),uint8_t(offset)};
}
VirtualCPU user(std::array<uint16_t,7> target={3,0x1d,0,0,1,0x1000,0x2000}) {
    auto c=machine();descriptor(c,7,1,{1,0x21,0x100,0,1,0,0});
    descriptor(c,7,2,{2,0x1d,0x1000,0x2000,0,0x1000,0x2000});
    descriptor(c,7,3,{3,0x19,0,0,1,0,0});descriptor(c,7,4,target);setc(c,0x7f,3);run(c,{0x26,0});
    for(auto p:{std::pair<unsigned,unsigned>{8,0x100},{9,1},{10,32},{11,0x2000},{12,0x1000},{13,0x2000},{14,2},{16,3},{0x20,7},{0x21,0},{0x22,0}}) {
        c.registers().r[7]=p.second;run(c,p.first<0x20?std::vector<uint8_t>{0x1f,uint8_t(0x40+p.first),7}:std::vector<uint8_t>{0x1f,0x97,uint8_t(p.first)},0x700);
    }
    setc(c,0x7f,7);run(c,{0x1f,0x12},0x700);check(c.mode()==Mode::User && !c.context(),"SP U fixture");
    // Trusted debugger mutations exercise malformed/live SS policies without
    // weakening IRET validation or making recovery depend on that user stack.
    auto& s=c.registers();s.ss=4;s.sb=0x1000;s.sp=0x1100;s.st=0x2000;s.flags=0x2f;s.r.fill(0xabcd);
    return c;
}
void exhaustive() {
    auto c=machine();
    for(unsigned byte=0;byte<2;++byte)for(unsigned store=0;store<2;++store)
    for(unsigned reg=0;reg<8;++reg)for(unsigned offset=0;offset<256;++offset) {
        auto& s=c.registers();s.ss=1;s.ds=2;s.sb=0x1000;s.sp=0x2000;s.st=0x3000;s.flags=0x2f;
        for(unsigned i=0;i<8;++i)s.r[i]=0xabc0+i;auto old=s;unsigned a=0x12000+offset;
        c.load(a,{0x80,0x93});c.load(a+0x10000,{0x66,0x55});auto time=c.protected_state()->time;
        run(c,code(0x30|(byte<<3)|(store<<6)|reg,offset));
        if(!store)old.r[reg]=byte?0x80:0x9380;
        check(!c.context() && s.pc==0x103 && s.r==old.r,"SP8 every byte/register/direction offset oracle");
        check(s.sp==old.sp && s.sb==old.sb && s.st==old.st && s.ss==old.ss && s.ds==old.ds && s.flags==old.flags,"SP8 preserves stack/pointers/NZCVI");
        check(c.peek(a)==(store?uint8_t(old.r[reg]):0x80) && c.peek(a+1)==(store&&!byte?uint8_t(old.r[reg]>>8):0x93),"SP8 transfer width and atomic word success");
        check(c.peek(a+0x10000)==0x66 && c.protected_state()->time==time+1,"SP8 uses SS and one retirement tick");
    }
    for(unsigned flags=0;flags<16;++flags)for(unsigned i:{0u,32u})for(unsigned tag:{0x30u,0x38u,0x77u,0x7fu}) {
        c.registers().flags=flags|i;run(c,code(tag,255));check(c.registers().flags==(flags|i),"SP8 preserves all incoming flag patterns");
    }
    c=machine();run(c,{0x26,0});auto saved=*c.context();
    c.registers().ss=1;c.registers().sp=0x2000;c.registers().sb=0x1000;c.registers().st=0x3000;c.registers().flags=0x2f;
    for(unsigned tag:{0x30u,0x38u,0x77u,0x7fu}) {
        run(c,code(tag,255),0x300);check(c.context() && *c.context()==saved && c.csr()==0x6f,"SP8 preserves E and complete active frame");
    }
}
void long_equivalence() {
    {
        auto fixture=machine();fixture.registers().ss=1;fixture.registers().sb=0x1000;fixture.registers().sp=0x2000;fixture.registers().st=0x3000;
        for(unsigned byte=0;byte<2;++byte)for(unsigned store=0;store<2;++store)
        for(unsigned reg=0;reg<8;++reg)for(unsigned offset:{0u,1u,127u,128u,254u,255u,256u}) {
            auto c=fixture;c.registers().r.fill(0xabcd);c.registers().flags=0x2f;c.load(0x12000+offset,{0x80,0x93});auto compact=c;
            unsigned tag=0x30|(byte<<3)|(store<<6)|reg;run(c,code(tag,offset,true));
            check(!c.context() && c.registers().pc==0x104,"all raw long SP forms retain length/behavior");
            check(store?c.peek(0x12000+offset)==0xcd:c.registers().r[reg]==(byte?0x80:0x9380),"long SP unsigned transfer oracle");
            if(offset<=255) {
                run(compact,code(tag,offset));
                check(compact.registers().r==c.registers().r && compact.csr()==c.csr() && compact.registers().sp==c.registers().sp && compact.peek(0x12000+offset)==c.peek(0x12000+offset) && compact.peek(0x12001+offset)==c.peek(0x12001+offset),"short/noncanonical long SP observable equivalence");
            }
        }
        auto c=fixture;run(c,code(0x30,65535,true));fault(c,Cause::Stack);
        check((*c.context())[19]==0,"SP off16 FFFF remains positive, never negative one");
    }
}
void decode_and_fetch() {

    auto fixture=user();
    for(unsigned tag=0;tag<256;++tag) {
        auto c=fixture;c.registers().sp=c.registers().st;c.load(0x10100,{0x28,uint8_t(tag)});c.map(0x10102,1,false);c.step();
        bool legal=(tag>=0x30 && tag<=0x3f)||(tag>=0x70 && tag<=0x7f);
        fault(c,legal?Cause::Memory:Cause::Illegal);
        if(legal)check(c.protected_state()->frames.back().memory[1]==1,"legal SP8 fetch beats invalid stack");
    }
    for(bool longer:{false,true})for(bool store:{false,true}) {
        auto b=code(store?0x70:0x30,longer?256:255,longer);
        for(unsigned missing=0;missing<b.size();++missing) {
            auto c=fixture;c.load(0x10100,b);c.map(0x10100+missing,1,false);auto old=c.registers();auto time=c.protected_state()->time;c.step();fault(c,Cause::Memory);
            check(c.protected_state()->frames.back().memory[1]==1 && c.registers().r==old.r && c.protected_state()->time==time,"SP partial fetch is atomic and never probes operand");
        }
        auto c=fixture;unsigned pc=65536-b.size();c.load(0x31100+(longer?256:255),{0x80,0x93});auto old=c.registers();auto time=c.protected_state()->time;
        run(c,b,pc);fault(c,Cause::Memory,pc);
        check(c.protected_state()->frames.back().memory[1]==3 && c.registers().r==old.r && c.peek(0x31100+(longer?256:255))==0x80 && c.peek(0x31101+(longer?256:255))==0x93 && c.protected_state()->time==time,"SP selected-PC failure rolls back loads/stores");
        c=fixture;run(c,b,pc-1);check(!c.context() && c.registers().pc==0xffff,"SP last valid following PC");
        for(unsigned available=1;available<b.size();++available) {
            c=fixture;pc=65536-available;run(c,b,pc);fault(c,Cause::Memory,pc);
            check(c.protected_state()->frames.back().memory[1]==1,"SP fetch cannot cross code segment");
        }
    }
    auto c=fixture;c.map(0x10103,1,false);run(c,code(0x30,0));check(!c.context() && c.registers().pc==0x103,"SP8 never fetches old fourth byte");
}
void stack_bounds() {
    auto fixture=user();
    for(bool byte:{false,true})for(bool store:{false,true})for(unsigned reg:{0u,7u})
    for(unsigned sb:{0x1000u,0x1041u})for(unsigned sp:{0xfffu,0x1000u,0x1020u,0x103fu,0x1040u,0x1041u,0xffffu})
    for(unsigned offset:{0u,1u,31u,127u,128u,255u}) {
        auto c=fixture;auto& s=c.registers();s.sb=sb;s.sp=sp;s.st=0x1040;auto old=s;
        uint64_t start=uint64_t(sp)+offset;unsigned width=byte?1:2;
        bool valid=sb<=sp && sp<=0x1040 && start+width<=0x1040;
        if(start+width<=65536)c.load(0x30000+start,byte?std::vector<uint8_t>{0x80}:std::vector<uint8_t>{0x80,0x93});
        run(c,code(0x30|(byte?8:0)|(store?64:0)|reg,offset));
        if(valid)check(!c.context() && s.sp==sp && (store||s.r[reg]==(byte?0x80:0x9380)),"SP allocated slot interval oracle");
        else {
            fault(c,Cause::Stack);check(c.registers().r==old.r && (*c.context())[11]==sp && (*c.context())[10]==old.flags,"SP invalid invariant/slot preserves original state");
            if(start+width<=65536)check(c.peek(0x30000+start)==0x80,"SP out-of-slot store has no first byte");
        }
    }
    for(bool byte:{false,true}) {
        auto c=fixture;c.registers().ss=2;c.registers().sp=0x1f00;c.load(0x21fff,{0x80,0x93});run(c,code(byte?0x38:0x70,255));
        if(byte)check(!c.context() && c.registers().r[0]==0x80,"SP8 +255 byte occupies last allocated slot");
        else {fault(c,Cause::Stack);check(c.peek(0x21fff)==0x80,"SP8 +255 partial word rejected atomically");}
    }
    auto edge=user({3,0x1d,0,0,1,0,0xffff});
    for(bool byte:{false,true})for(bool store:{false,true})for(unsigned offset:byte?std::vector<unsigned>{254,255}:std::vector<unsigned>{253,254}) {
        auto c=edge;c.registers().sb=0;c.registers().sp=0xff00;c.registers().st=0xffff;
        unsigned width=byte?1:2;bool valid=0xff00+offset+width<=0xffff;
        if(valid)c.load(0x3ff00+offset,byte?std::vector<uint8_t>{0x80}:std::vector<uint8_t>{0x80,0x93});
        run(c,code(0x30|(byte?8:0)|(store?64:0),offset));
        if(valid)check(!c.context() && c.registers().sp==0xff00,"SP8 last allocated byte/word at ST=FFFF");
        else fault(c,Cause::Stack);
    }
    auto absent=user({});absent.registers().sp=absent.registers().st;run(absent,code(0x30,0));fault(absent,Cause::Stack);
    check((*absent.context())[19]==0,"SP stack check precedes absent SS translation");
    auto c=fixture;c.registers().sp=0x2000;c.registers().st=0x2000;run(c,code(0x70,0),0xfffd);fault(c,Cause::Stack,0xfffd);
    check(c.peek(0x32000)==0,"SP operand Stack fault outranks invalid selected next PC");
}
void protection() {
    for(unsigned reason:{4u,5u,6u,7u,8u})for(bool store:{false,true}) {
        std::array<uint16_t,7> d={3,0x1d,0,0,1,0x1000,0x2000};
        if(reason==4)d={};
        if(reason==5){d[3]=0x1101;d[4]=0;d[6]=0x1101;}
        if(reason==6)d[1]=store?0x0d:0x15;
        if(reason==7){d[0]=255;d[5]=0;d[6]=0x100;}
        auto c=user(d);if(reason==7){c.registers().sb=0;c.registers().sp=0x80;c.registers().st=0x100;}
        c.load(0x31100,{0x5a,0xa5});if(reason==8)c.map(0x31101,1,false);
        BoundaryEvent event{c.run(),c.boundary(),c.next_sequence(),BoundaryEvent::Kind::ConsoleInput};event.data={0x41};
        check(c.apply_boundary_events({event})==HostError::None,"SP device fixture");auto old=c.registers();auto time=c.protected_state()->time;auto commits=c.protected_state()->device_commits;
        run(c,code(store?0x70:0x30,0));fault(c,Cause::Memory);auto m=c.protected_state()->frames.back().memory;
        check(m[0]==1 && m[1]==2 && m[3]==7 && m[4]==4 && m[9]==2 && m[10]==reason,"SP SS bounds/rights/kind/backing denial metadata");
        check(c.registers().r==old.r && (*c.context())[11]==old.sp && c.protected_state()->time==time && c.peek(0x31100)==0x5a,"denied SP transfer is atomic");
        check(c.protected_state()->device_commits==commits && c.protected_state()->console_rx==std::vector<uint8_t>{0x41} && c.protected_state()->console_tx.empty(),"SP RAM/MMIO mismatch never consumes or emits");
        if(reason==8)check(m[7]==0x1101 && m[8]==0x104 && (*c.context())[18]==0x1101 && (*c.context())[19]==0x103,"SP failed second byte retains exact relevant addresses");
    }
    // A K stack mapped to a DEVICE cannot support recovery entry either. Retain
    // the original precise operand fault before the expected terminal entry.
    auto c=machine();descriptor(c,0,4,{255,0xc3,0,0x100,0,0,0});c.registers().ss=4;c.registers().sb=0;c.registers().sp=0x82;c.registers().st=0x100;
    auto commits=c.protected_state()->device_commits;run(c,code(0x70,0));
    check(c.state()==ExecutionState::TerminalDoubleFault && !c.protected_state()->frames.empty(),"invalid K device stack has terminal recovery failure");
    const auto& f=c.protected_state()->frames.front();check(f.words[8]==0x100 && f.words[17]==2 && f.memory[1]==2 && f.memory[10]==7,"K device SS retains original SP operand fault");
    check(c.protected_state()->device_commits==commits && c.protected_state()->console_tx.empty(),"SP device denial commits no MMIO transaction");
}
}
int main() {
    try{exhaustive();long_equivalence();decode_and_fetch();stack_bounds();protection();std::cout<<checks<<" compact SP checks passed\n";}
    catch(const std::exception& e){std::cerr<<"compact SP: "<<e.what()<<'\n';return 1;}
}
