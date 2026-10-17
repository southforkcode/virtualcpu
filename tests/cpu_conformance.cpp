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
VirtualCPU machine(Revision revision=Revision::V05) {
    VirtualCPU c(revision,0x40000);
    for (unsigned i=0;i<=6;++i) vector_to(c,i,i?0x300:0x100);
    c.load(0x300,{0x1f,0x12}); c.reset(); return c;
}
void code(VirtualCPU& c, std::initializer_list<uint8_t> b, uint16_t pc=0x100) {
    c.load(pc,std::vector<uint8_t>(b)); c.registers().pc=pc;
}
void faulted(const VirtualCPU& c, Cause cause, uint16_t pc=0x100) {
    check(c.context().has_value(),"missing context");
    check((*c.context())[17]==unsigned(cause),"wrong cause");
    check((*c.context())[8]==pc,"imprecise fault PC");
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
void traps() {
    for (int service=0;service<256;++service) for (int f=0;f<32;++f) {
        auto c=machine(); code(c,{0x26,uint8_t(service)}); auto& s=c.registers();
        s.flags=uint16_t((f&15)|((f&16)?32:0)); s.r[4]=0x9876; s.dp=0x5678; s.ds=3; s.ss=2;
        auto before=s; c.step();
        faulted(c,Cause::Trap,0x102); check((*c.context())[20]==service,"trap number");
        check(c.csr()==((before.flags&15)|64) && (*c.context())[10]==before.flags,"trap flags");
        s.r.fill(0xabcd); s.ds=0; s.ss=0; s.dp=0;
        c.step(); check(!c.context() && c.csr()==before.flags && s.pc==0x102 && s.r==before.r && s.dp==before.dp && s.ds==3 && s.ss==2,"full IRET restore");
    }
    for (int invalid : {0x40,0x10,0x6f,0x8000}) {
        auto c=machine(); code(c,{0x26,1}); c.load(0x300,{0x1f,0x4a,0,0x1f,0x2a,1,0x1f,0x12});
        c.step(); c.registers().r[0]=invalid; c.step(); c.step();
        check(c.registers().r[1]==invalid && (*c.context())[10]==invalid,"SETX unmasked CSR"); c.step();
        check(c.state()==ExecutionState::TerminalDoubleFault && (*c.context())[10]==invalid,"invalid IRET retains context");
    }
    for (int failure=0;failure<6;++failure) {
        auto c=machine(); code(c,{0x26,0xee});
        if (failure<4) c.map(0x18+failure,1,false);
        if (failure==4) c.load(0x1b,{1});
        if (failure==5) c.map(0x300,1,false);
        c.step(); check(c.state()==ExecutionState::TerminalDoubleFault && c.registers().pc==0x100,"entry failure path");
        faulted(c,Cause::Trap,0x102); check((*c.context())[20]==0xee,"entry retains trap");
    }
    for (int opcode : {0x26,0x34}) {
        auto c=machine(Revision::V04);
        if (opcode==0x26) { code(c,{0x26},0xffff); c.step(); }
        else { code(c,{0x1f,0x34},0xfffe); c.step(); }
        faulted(c,Cause::Illegal,opcode==0x26?0xffff:0xfffe);
    }
    for (int pc : {0xfffd,0xfffe,0xffff}) {
        auto c=machine(); code(c,{0x26},pc); if (pc<0xffff) c.load(pc+1,{7}); c.step();
        if (pc==0xfffd) faulted(c,Cause::Trap,0xffff); else faulted(c,Cause::Memory,pc);
    }
    auto c=machine(); code(c,{0x26,1}); c.registers().sp=0; c.registers().sb=1; c.step();
    faulted(c,Cause::Trap,0x102); c.step(); check(c.state()==ExecutionState::TerminalDoubleFault,"invalid saved stack return");
    c=machine(); code(c,{0x26,1}); c.map(0x102,1,false); c.step(); faulted(c,Cause::Trap,0x102); c.step();
    check(c.state()==ExecutionState::TerminalDoubleFault && c.diagnostic()->cause==Cause::Memory,"IRET missing continuation");
    c=machine(); code(c,{0x26,1}); c.load(0x300,{0x26,2}); c.step(); auto saved=*c.context(); c.step();
    check(c.state()==ExecutionState::TerminalDoubleFault && *c.context()==saved,"nested SWI retains context");
    c=machine(); code(c,{0x1f,0x28,8}); c.step(); faulted(c,Cause::Illegal);
}
void interrupts_reset() {
    auto c=machine(); code(c,{3,2,1}); c.request_irq(); c.step();
    check(c.registers().pc==0x101 && !c.context(),"EI boundary"); c.step(); faulted(c,Cause::IRQ,0x101);
    check(!c.irq_pending(),"IRQ consumed"); c.request_irq(); c.step(); check(!c.context(),"IRQ return"); c.step(); faulted(c,Cause::IRQ,0x101);
    c=machine(); code(c,{1,0}); c.registers().flags=32; c.step();
    check(c.state()==ExecutionState::Halted && c.registers().pc==0x101,"HALT continuation"); c.request_irq(); c.step();
    faulted(c,Cause::IRQ,0x101); c.step(); check(c.state()==ExecutionState::Running && c.registers().pc==0x101,"HALT wake return");
    c=machine(); code(c,{0x26,1}); c.load(0x300,{3,1,0x1f,0x12}); c.step(); c.request_irq(); c.step(); c.step();
    check(c.state()==ExecutionState::Halted && c.csr()==96 && c.irq_pending(),"handler HALT policy");
    check(!c.step(),"halted handler does not progress"); c.load(0x2000,{0xaa}); c.reset();
    check(c.state()==ExecutionState::Running && c.registers().pc==0x100 && !c.context() && !c.irq_pending() && c.peek(0x2000)==0xaa,"reset state/RAM");
    c.map(0,1,false); c.reset(); check(c.state()==ExecutionState::TerminalResetFault && !c.context() && c.registers().pc==0,"reset fault");
    c.request_irq(); check(!c.irq_pending(),"terminal state ignores IRQ");
    c.map(0,1,true); c.reset(); check(c.state()==ExecutionState::Running,"reset terminal recovery");
}
void decode_and_recovery() {
    for (Revision rev : {Revision::V04,Revision::V05}) {
        for (unsigned op=0;op<256;++op) {
            auto c=machine(rev); code(c,{uint8_t(op)},0xffff); c.step();
            bool assigned=op<=3 || (op>=0x10 && op<=0x26) || (op>=0x40 && op<=0x4e) || op==0x52 || op==0x53;
            if (rev==Revision::V04 && op==0x26) assigned=false;
            check(c.context().has_value(),"primary boundary must enter");
            check(((*c.context())[17]!=1)==assigned,"primary decoder coverage");
        }
        for (unsigned prefix : {0x10,0x15,0x1a,0x1f}) for (unsigned x=0;x<256;++x) {
            auto c=machine(rev); code(c,{uint8_t(prefix),uint8_t(x)},0xfffe); c.step();
            bool assigned=prefix==0x10 ? x<0xe0 : prefix==0x15 ? x<0x40 || x>=0x50 : prefix==0x1a ?
                x<16 || (x>=0x10 && x<=0x13) || (x>=0x20 && x<=0x23) || (x>=0x30 && x<=0x33) :
                x<=0x12 || (x>=0x20 && x<=(rev==Revision::V05?0x34u:0x33u)) || (x>=0x40 && x<=0x50) || (x>=0x60 && x<=0x63);
            check(!(c.context() && (*c.context())[17]==1)==assigned,"extension decoder coverage");
        }
    }
    for (unsigned reg=0;reg<256;++reg) {
        auto c=machine(); code(c,{0x1f,0x34,uint8_t(reg)}); c.step(); faulted(c,reg<8?Cause::Invalid:Cause::Illegal);
    }
    for (unsigned index=0;index<21;++index) for (unsigned reg=0;reg<8;++reg) {
        auto c=machine(); code(c,{0x26,0xab}); c.load(0x300,{0x1f,uint8_t(0x20+index),uint8_t(reg)});
        c.step(); auto expected=(*c.context())[index]; c.step(); check(c.registers().r[reg]==expected,"GETX every field/register");
    }
    for (unsigned flag=0;flag<32;++flag) for (bool active : {false,true}) {
        auto c=machine();
        if (active) { code(c,{0x26,1}); c.step(); }
        auto pc=c.registers().pc;
        c.load(pc,{0x10,0x93}); c.registers().r[0]=(flag&15)|((flag&16)?32:0)|64; c.step();
        check(c.csr()==((flag&15)|((flag&16)?32:0)|(active?64:0)),"CSR write ignores supplied E");
    }
    for (int bad : {9,10,11,14,16}) {
        auto c=machine(); code(c,{0x26,3});
        c.load(0x300,{0x1f,uint8_t(0x40+bad),0,0x1f,0x12}); c.step();
        c.registers().r[0]=bad==11?0xffff:bad==10?0x10:0x100;
        c.step(); auto saved=*c.context(); c.step();
        check(c.state()==ExecutionState::TerminalDoubleFault && *c.context()==saved,"IRET validation retains edits");
        check(c.diagnostic()->cause==(bad==11?Cause::Stack:Cause::Invalid),"IRET field priority");
    }
    // Recover an invalid live/saved stack without touching ordinary stack RAM.
    auto c=machine(); code(c,{0x10,0xd3,0xff,0}); c.registers().r[0]=0x1000;
    c.load(0x300,{0x1f,0x2d,0,0x1f,0x4b,0,0x15,0,3,1,0x1f,0x48,0,0x1f,0x12});
    c.map(0x1000,0xf000,false); c.step(); c.step(); faulted(c,Cause::Illegal,0x102);
    for (int i=0;i<5;++i) c.step();
    check(!c.context() && c.registers().sp==0x1000 && c.registers().pc==0x103 && c.registers().r[0]==0x1000,"stack repair without RAM");
    // Actual saved CSR/PC recovery and live-handler register discard.
    c=machine(); code(c,{0xff,0}); c.load(0x300,{0x15,0,1,1,0x1f,0x48,0,0x15,0,5,0,0x1f,0x4a,0,0x1f,0x12});
    c.registers().r[0]=0xbeef; c.step(); for (int i=0;i<5;++i) c.step();
    check(c.registers().pc==0x101 && c.csr()==5 && c.registers().r[0]==0xbeef,"recover known resume PC/CSR");
    // An IRQ observes the committed first allocation, not a whole prologue.
    c=machine(); c.registers().sb=0x1000; c.registers().sp=c.registers().st=0x10c8; c.registers().flags=32;
    code(c,{0x25,0x80,0x25,0x80}); c.step(); c.request_irq(); c.step(); faulted(c,Cause::IRQ,0x102);
    check((*c.context())[11]==0x1048,"IRQ between adjustments"); c.step(); c.step();
    faulted(c,Cause::Stack,0x102); check((*c.context())[11]==0x1048,"post-IRQ allocation fault");
    // Report the first unavailable byte, while retaining instruction-start PC.
    c=machine(); code(c,{0x15,0,0x34,0x12}); c.map(0x103,1,false); c.step(); faulted(c,Cause::Memory);
    check((*c.context())[18]==0x103 && (*c.context())[19]==0x100,"missing later instruction byte address");
    c=machine(); c.registers().ds=3; code(c,{0x15,0x50,0,0x20}); c.map(0x32001,1,false); c.step();
    check((*c.context())[18]==0x2001 && (*c.context())[19]==0x103,"missing later store byte address");
    // Invalid input state takes priority over unrepresentable following PC.
    c=machine(); code(c,{0x24,0xff},0xfffe); c.registers().dp=0; c.step(); faulted(c,Cause::Invalid,0xfffe);
    c=machine(); code(c,{0x25,0},0xfffe); c.registers().sp=0; c.registers().sb=1; c.step(); faulted(c,Cause::Stack,0xfffe);
    // Pending requests survive failed entry and successful SWI.
    c=machine(); code(c,{0x26,1}); c.request_irq(); c.step(); check(c.irq_pending(),"SWI does not consume IRQ");
    c=machine(); c.registers().flags=32; c.request_irq(); c.map(0x300,1,false); c.step();
    check(c.irq_pending() && c.state()==ExecutionState::TerminalDoubleFault,"failed IRQ entry retains request");
    // Fault in an established handler must retain live I, registers and frame.
    c=machine(); code(c,{0x26,1}); c.load(0x300,{3,0xff}); c.step(); c.step(); auto frame=*c.context(); c.step();
    check(c.csr()==96 && c.registers().pc==0x301 && *c.context()==frame,"nested fault preserves handler state");
}
void worked_images() {
    std::ifstream file(SOURCE_ROOT "/doc/ISA_Examples.md"); std::string line;
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
    const int steps[]={68,50,162,55,101,9,48,22,22,6};
    for (unsigned image_index=0;image_index<20;++image_index) {
        unsigned n=image_index%10;
        auto c=machine(image_index<10?Revision::V04:Revision::V05); c.load(0x100,images[n]);
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
    try { if (argc==1) { arithmetic(); addressing(); stacks(); traps(); interrupts_reset(); decode_and_recovery(); } worked_images(); }
    catch (const std::exception& e) { std::cerr<<"FAIL after "<<checks<<" checks: "<<e.what()<<'\n'; return 1; }
    std::cout<<checks<<" conformance assertions passed\n";
}
