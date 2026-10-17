#include "cpu.hpp"
#include "density_example.hpp"
#include <algorithm>
#include <iostream>
#include <stdexcept>
#include <tuple>
using namespace vcpu;
namespace {
uint64_t checks=0;
void check(bool b,const char* s){++checks;if(!b)throw std::runtime_error(s);}
const ProtectedState& ps(const VirtualCPU& c){return *c.protected_state();}
auto regs(const Registers& r){return std::tie(r.r,r.pc,r.cs,r.ds,r.ss,r.sp,r.sb,r.st,r.dp,r.flags);}
void run(VirtualCPU& c,const std::vector<uint8_t>& b,unsigned pc=0x700){c.load(pc,b);c.registers().pc=pc;c.step();}
void setc(VirtualCPU& c,unsigned id,unsigned value){c.registers().r[7]=value;run(c,{0x1f,0xb7,uint8_t(id)});check(!c.context(),"control setup");}
void setx(VirtualCPU& c,unsigned id,unsigned value){c.registers().r[7]=value;run(c,id<32?std::vector<uint8_t>{0x1f,uint8_t(0x40+id),7}:std::vector<uint8_t>{0x1f,0x97,uint8_t(id)});}
void desc(VirtualCPU& c,unsigned map,unsigned seg,std::array<uint16_t,7> d){setc(c,0x20,map);setc(c,0x21,seg);for(unsigned i=0;i<7;++i)setc(c,0x22+i,d[i]);setc(c,0x7f,1);}
VirtualCPU machine(MachineProfile profile=MachineProfile::MinimalV1){VirtualCPU c(0x80000,profile);for(unsigned i=0;i<exception_vector_count;++i)c.load(4*i,{uint8_t(i?0x00:0x00),uint8_t(i?3:1),0,0});c.load(0x300,{0x1f,0x12});c.reset();for(unsigned i=0;i<5;++i)setc(c,0x10+i,i<3?0:0xfffe);setc(c,0x7f,5);return c;}
VirtualCPU user(uint16_t data_flags=0x19){auto c=machine();desc(c,7,1,{1,0x21,0x100,0x400,0,0,0});desc(c,7,2,{5,0x1d,0x1000,0x2000,0,0x1000,0x2000});desc(c,7,3,{3,data_flags,0,0,1,0,0});setc(c,0x7f,3);run(c,{0x26,0});for(auto [id,value]:std::vector<std::pair<unsigned,unsigned>>{{8,0x100},{9,1},{10,0x2c},{11,0x2000},{12,0x1000},{13,0x2000},{14,2},{16,3},{32,7},{33,0},{34,0}})setx(c,id,value);c.registers().r[7]=7;run(c,{0x1f,0xb7,0x7f});run(c,{0x1f,0x12});check(c.mode()==Mode::User && !c.context(),"user bootstrap");return c;}
void instruction(VirtualCPU& c,const std::vector<uint8_t>& b,unsigned pc=0x100){c.load((unsigned(c.registers().cs)<<16)+pc,b);c.registers().pc=pc;check(c.step(),"step");}
void captured(const VirtualCPU& c,const Registers& before,unsigned cause){check(c.context() && ps(c).frames.back().words[17]==cause,"expected fault");const auto& w=ps(c).frames.back().words;check(std::equal(before.r.begin(),before.r.end(),w.begin()) && w[8]==before.pc && w[9]==before.cs && w[10]==before.flags,"precise saved register/flag/PC state");}
void units(){for(bool in_user:{false,true}){auto c=in_user?user():machine();for(bool decrement:{false,true})for(unsigned dst=0;dst<4;++dst)for(unsigned flags:{0u,15u,32u,47u}){
 c.load((unsigned(c.registers().cs)<<16)+0x100,{0x1a,uint8_t((decrement?0xf4:0xf0)+dst)});
 for(unsigned value=0;value<65536;++value){auto& r=c.registers();r.pc=0x100;r.r={1,2,3,4,0x1111,0x2222,0x3333,0x4444};r.r[dst]=value;r.flags=flags;auto expected=r;auto time=ps(c).time;int32_t signed_value=value<32768?int32_t(value):int32_t(value)-65536;auto wide=signed_value+(decrement?-1:1);uint16_t result=uint16_t(value+(decrement?-1:1));expected.r[dst]=result;expected.pc+=2;expected.flags=(flags&~15)|(result?0:1)|(result&0x8000?2:0)|((decrement?value!=0:value==65535)?4:0)|((wide< -32768 || wide>32767)?8:0);c.step();check(!c.context() && regs(r)==regs(expected) && ps(c).time==time+1,"exhaustive unit arithmetic flags/operands/retirement");}
 }}
 auto c=machine();for(bool dec:{false,true}){c.registers().r[0]=0xffff;c.registers().flags=47;c.registers().pc=0xfffe;auto old=c.registers();instruction(c,{0x1a,uint8_t(dec?0xf4:0xf0)},0xfffe);captured(c,old,2);c=machine();}
}
std::vector<uint8_t> post(bool store,unsigned width,unsigned value=0,unsigned base=4){return {0x15,uint8_t(0x40|(store?8:0)|(width==1?4:0)|value),uint8_t(base-4)};}
void post_transfers(){
 for(bool in_user:{false,true})for(bool active:{false,true}){if(in_user && active)continue;auto seed=in_user?user():machine();if(active)run(seed,{0x26,19});
 for(bool store:{false,true})for(unsigned width:{1u,2u})for(unsigned value=0;value<4;++value)for(unsigned base=4;base<8;++base)for(unsigned flags=0;flags<64;++flags)if(!(flags&16)){
  auto c=seed;c.registers().ds=in_user?3:1;c.registers().r={0xabcd,0xbcde,0xcdef,0xdef0,0x1111,0x2222,0x3333,0x4444};c.registers().r[base]=0x2001;c.registers().flags=flags;auto expected=c.registers();auto previous=c.context();unsigned physical=(in_user?0x30000:0x10000)+0x2001;c.load(physical,{0x80,0x93});expected.pc=0x103;expected.r[base]+=width;if(!store)expected.r[value]=width==1?0x80:0x9380;auto time=ps(c).time;auto commits=ps(c).device_commits;c.measure_traffic(true);instruction(c,post(store,width,value,base));
  check(regs(c.registers())==regs(expected) && c.context()==previous && c.csr()==(flags|(active?64:0)),"post forms sample old values and preserve all flags/frame state");
  check(ps(c).time==time+1 && ps(c).device_commits==commits && c.step_traffic().ram_reads==3+(store?0:width) && c.step_traffic().ram_writes==(store?width:0),"post transfer is one RAM-only retirement");
  if(store)check(c.peek(physical)==uint8_t(expected.r[value]) && c.peek(physical+1)==(width==2?uint8_t(expected.r[value]>>8):0x93),"post store commits exact width");
 }}
 auto seed=machine();
 for(bool store:{false,true})for(unsigned width:{1u,2u})for(unsigned base:{0xfffdu,0xfffeu,0xffffu}){
  auto c=seed;c.registers().ds=1;c.registers().r[4]=base;c.registers().r[0]=0xabcd;c.registers().pc=0x100;auto before=c.registers();auto time=ps(c).time;c.load(0x10000+base,{0x80});if(base<65535)c.load(0x10001+base,{0x93});c.measure_traffic(true);instruction(c,post(store,width));
  if(base+width>65535){captured(c,before,4);check(ps(c).time==time && c.step_traffic().ram_reads==7 && !c.step_traffic().ram_writes && c.peek(0x10000+base)==0x80,"advance overflow precedes operand probing/reads and discards transfer");}
  else check(!c.context() && c.registers().r[4]==base+width,"largest representable post advance");
 }
 // Ordinary final tail access succeeds even though its fused counterpart fails.
 for(unsigned width:{1u,2u}){auto c=seed;unsigned base=65536-width;c.registers().ds=1;c.registers().r[4]=base;c.load(0x10000+base,width==1?std::vector<uint8_t>{0x80}:std::vector<uint8_t>{0x80,0x93});instruction(c,{0x15,uint8_t(width==1?0x88:0x80),0});check(!c.context() && c.registers().r[4]==base && c.registers().r[0]==(width==1?0x80:0x9380),"ordinary byte/word tail remains legal without increment");}
 for(bool store:{false,true}){
  auto c=seed;c.registers().ds=1;c.registers().r[4]=0x2000;c.registers().r[0]=0xabcd;c.registers().pc=0xfffd;auto before=c.registers();c.load(0x12000,{0x80,0x93});c.measure_traffic(true);instruction(c,post(store,2),0xfffd);captured(c,before,2);check(c.step_traffic().ram_reads==7 && !c.step_traffic().ram_writes && c.peek(0x12000)==0x80,"bad next PC discards post RAM read/store and pointer update");
  c=seed;c.registers().ds=1;c.registers().r[4]=0x2000;c.registers().r[0]=0xabcd;c.load(0x12000,{0x80,0x93});c.map(0x12001,1,false);before=c.registers();before.pc=0x100;instruction(c,post(store,2));captured(c,before,2);check(ps(c).frames.back().memory[9]==2 && ps(c).frames.back().memory[10]==8 && c.peek(0x12000)==0x80,"post whole-word hole preflight before any RAM transfer");
 }
 for(unsigned tag=0x40;tag<=0x4f;++tag)for(unsigned descriptor=4;descriptor<256;++descriptor){auto c=seed;c.registers().r[4]=0xffff;c.map(0x103,1,false);instruction(c,{0x15,uint8_t(tag),uint8_t(descriptor)});check(c.context() && (*c.context())[17]==1,"reserved descriptor rejected before pointer/operand/following fetch");}
 for(unsigned missing:{1u,2u}){auto c=seed;c.map(0x100+missing,1,false);c.registers().r[4]=0xffff;instruction(c,{0x15,0x40,0});check(c.context() && ps(c).frames.back().words[17]==2 && ps(c).frames.back().memory[1]==1,"post truncation precedes pointer overflow");}
 for(auto profile:{MachineProfile::MinimalV1,MachineProfile::ConsoleV1})for(bool store:{false,true})for(unsigned width:{1u,2u}){
  auto c=machine(profile);if(profile==MachineProfile::ConsoleV1){BoundaryEvent event{c.run(),c.boundary(),c.next_sequence(),BoundaryEvent::Kind::ConsoleInput};event.data={0x41};check(c.apply_boundary_events({event})==HostError::None,"console input seed");}
  c.registers().ds=255;c.registers().r[4]=profile==MachineProfile::ConsoleV1?0x80:0;auto before=c.registers();before.pc=0x100;auto commits=ps(c).device_commits;auto rx=ps(c).console_rx;c.measure_traffic(true);instruction(c,post(store,width));captured(c,before,2);check(ps(c).frames.back().memory[10]==7 && ps(c).device_commits==commits && !c.step_traffic().mmio_samples && ps(c).console_rx==rx && ps(c).console_tx.empty(),"post MMIO rejection has no device samples/commits or consumed RX");
 }
 // Retry the same fault site after installing a previously absent RAM byte.
 auto c=seed;c.registers().ds=1;c.registers().r[4]=0x2000;c.load(0x12000,{0x55});c.map(0x12000,1,false);instruction(c,post(false,1));c.map(0x12000,1,true);c.step();check(!c.context() && c.registers().pc==0x100 && c.registers().r[4]==0x2000,"IRET retries uncommitted post transfer");c.step();check(!c.context() && c.registers().r[4]==0x2001 && c.registers().r[0]==0x55,"repaired post transfer advances once");
}

void relative_calls(){
 for(bool in_user:{false,true})for(bool short_form:{false,true})for(unsigned flags:{0u,15u,32u,47u}){
  auto c=in_user?user():machine();c.registers().flags=flags;auto old=c.registers();unsigned length=short_form?2:3;unsigned physical=in_user?0x50000:0;c.load(physical+old.sp-2,{0x55,0xaa});auto time=ps(c).time;
  instruction(c,short_form?std::vector<uint8_t>{0x56,0x1e}:std::vector<uint8_t>{0x57,0x1d,0});
  check(!c.context() && c.registers().pc==0x120 && c.registers().cs==old.cs && c.registers().sp==old.sp-2 && c.registers().flags==old.flags && ps(c).time==time+1,"CALLR samples next and commits same-CS near frame with unchanged flags");
  check(c.peek(physical+old.sp-2)==uint8_t(0x100+length) && c.peek(physical+old.sp-1)==1,"CALLR stores selected short/word end as return address");
  instruction(c,{0x1f,0x11},0x120);check(c.registers().pc==0x100+length && c.registers().sp==old.sp && c.registers().flags==old.flags,"RET restores CALLR frame in U/K");
 }
 auto seed=machine();
 for(bool short_form:{false,true})for(int displacement:short_form?std::vector<int>{-128,-1,0,1,127}:std::vector<int>{-32768,-1,0,1,32767}){
  auto c=seed;unsigned pc=short_form?0x100:0x8000;std::vector<uint8_t> bytes{uint8_t(short_form?0x56:0x57),uint8_t(displacement)};if(!short_form)bytes.push_back(uint8_t(uint16_t(displacement)>>8));c.registers().pc=pc;auto before=c.registers();auto dest=int(pc)+int(bytes.size())+displacement;auto time=ps(c).time;instruction(c,bytes,pc);
  if(dest>65535 || dest<0){captured(c,before,2);check(ps(c).time==time && c.peek(before.sp-2)==0 && c.peek(before.sp-1)==0,"CALLR target never wraps and cannot commit return word");}
  else check(!c.context() && c.registers().pc==dest && c.registers().sp==before.sp-2,"CALLR signed endpoints from full end");
 }
 for(bool short_form:{false,true})for(unsigned error=0;error<5;++error){auto c=seed;unsigned length=short_form?2:3;auto bytes=short_form?std::vector<uint8_t>{0x56,0}:std::vector<uint8_t>{0x57,0,0};unsigned pc=error==0?65536-length:0x100;
  if(error==1)c.registers().sp=0;
  if(error==2)c.map(c.registers().sp-1,1,false);
  if(error==3){bytes[1]=0x20;c.map(pc+length+0x20,1,false);}
  if(error==4){bytes[1]=0x80;if(!short_form)bytes[2]=0xff;pc=0;}
  c.registers().pc=pc;auto before=c.registers();auto time=ps(c).time;c.measure_traffic(true);instruction(c,bytes,pc);captured(c,before,error==1?3:2);
  check(ps(c).time==time && !c.step_traffic().ram_writes,"CALLR error has no partial frame, SP or retirement");
 }
 // Following-PC range check beats a valid backward target, but stack faults
 // still take the same priority as absolute near CALL.
 for(bool short_form:{false,true}){auto c=seed;unsigned n=short_form?2:3;c.registers().sp=0;auto bytes=short_form?std::vector<uint8_t>{0x56,0xfe}:std::vector<uint8_t>{0x57,0xfd,0xff};c.registers().pc=65536-n;auto before=c.registers();instruction(c,bytes,65536-n);captured(c,before,3);}
 // Truncated operands and nested calls retain the original decoded PC/frames.
 for(unsigned opcode:{0x56u,0x57u}){auto c=seed;c.map(0x101,1,false);c.registers().sp=0;instruction(c,{uint8_t(opcode),0,0});check(c.context() && ps(c).frames.back().words[17]==2 && ps(c).frames.back().memory[1]==1,"CALLR fetch fault before stack validation");}
 auto c=seed;run(c,{0x26,7});auto frame=*c.context();auto sp=c.registers().sp;instruction(c,{0x56,2});check(c.context()==frame && (c.csr()&64) && c.registers().sp==sp-2,"CALLR in handler preserves active exception authority/E");instruction(c,{0x56,2},0x104);instruction(c,{0x1f,0x11},0x108);check(c.registers().pc==0x106,"nested relative call returns to inner continuation");instruction(c,{0x1f,0x11},0x106);check(c.registers().pc==0x102 && c.registers().sp==sp && c.context()==frame,"nested relative calls restore stack without editing protected frame");
}

void sign_extension(){
 for(bool in_user:{false,true})for(bool active:{false,true}){if(in_user && active)continue;auto c=in_user?user():machine();if(active)run(c,{0x26,21});auto frame=c.context();for(unsigned dst=0;dst<4;++dst)for(unsigned flags:{0u,4u,8u,12u,15u,32u,36u,40u,44u,47u}){
 c.load((unsigned(c.registers().cs)<<16)+0x100,{0x1a,uint8_t(0xf8+dst)});
 for(unsigned value=0;value<65536;++value){auto& r=c.registers();r.pc=0x100;r.r={1,2,3,4,0x1111,0x2222,0x3333,0x4444};r.r[dst]=value;r.flags=flags;auto expected=r;auto result=uint16_t((value&255)>=128?(value&255)|0xff00:value&255);expected.r[dst]=result;expected.flags=(flags&~3)|(result?0:1)|(result&0x8000?2:0);expected.pc+=2;auto time=ps(c).time;c.step();check(c.context()==frame && regs(r)==regs(expected) && c.csr()==(expected.flags|(active?64:0)) && ps(c).time==time+1,"all byte/upper-byte values, B0 destinations, CVIE and U/K/frame authority for SEXT");}
 }}
 for(unsigned dst=0;dst<4;++dst){auto c=machine();c.registers().r[dst]=0x1280;c.registers().pc=0xfffe;c.registers().flags=47;auto old=c.registers();instruction(c,{0x1a,uint8_t(0xf8+dst)},0xfffe);captured(c,old,2);}
}
void interrupt_boundaries(){
 for(auto bytes:std::vector<std::vector<uint8_t>>{{0x1a,0xf0},{0x15,0x48,0},{0x56,0},{0x1a,0xf8}}){auto c=machine();c.registers().ds=255;c.registers().r[0]=2;run(c,{0x15,0x50,2,0});c.registers().ds=1;c.registers().r[0]=0x80;c.registers().r[4]=0x2000;c.registers().pc=0x100;c.registers().flags=47;c.load(0x100,bytes);auto old=c.registers();auto time=ps(c).time;BoundaryEvent edge{c.run(),c.boundary(),c.next_sequence(),BoundaryEvent::Kind::Edge};edge.source=1;check(c.apply_boundary_events({edge})==HostError::None,"IRQ edge accepted");auto replay=c;
  c.step();replay.step();captured(c,old,5);check(ps(c).time==time && ps(c).in_service==2 && c.peek(0x12000)==0,"IRQ before density instruction has no early effects");
  c.step();replay.step();check(!c.context() && regs(c.registers())==regs(old),"IRQ IRET restores exact instruction inputs");c.step();replay.step();check(!c.context() && ps(c).time==time+2 && regs(c.registers())==regs(replay.registers()) && c.boundary()==replay.boundary() && ps(c).pending==ps(replay).pending && ps(c).in_service==2 && c.peek(0x12000)==replay.peek(0x12000),"density instructions commit once after IRQ, deterministic replay");
 }
}
void assembled_example(){auto c=machine();c.load(0x100,density_example::code);c.load(0x4200,{0x80});c.registers().pc=0x100;auto replay=c;unsigned steps=0;while(c.state()==ExecutionState::Running && ++steps<40){c.step();replay.step();check(regs(c.registers())==regs(replay.registers()) && c.context()==replay.context() && c.csr()==replay.csr() && c.boundary()==replay.boundary(),"assembled density example replay");}check(c.state()==ExecutionState::Halted && !c.context() && c.registers().r[0]==0xffff && c.registers().r[1]==0xff81 && c.registers().r[4]==0x4201 && c.registers().r[5]==0x4302 && c.registers().sp==0xfffe && c.peek(0x4000)==0xff && c.peek(0x4001)==0xff && c.peek(0x4300)==0x80 && c.peek(0x4301)==0xff,"production-assembled density example and near-call ABI");}

void rights_and_overlap(){
 for(bool in_user:{false,true})for(bool store:{false,true}){auto c=in_user?user(store?0x09:0x11):machine();if(!in_user)desc(c,0,1,{1,uint16_t(store?0x41:0x81),0,0,1,0,0});c.registers().ds=in_user?3:1;c.registers().r[4]=0x2000;c.registers().r[0]=0xabcd;c.registers().pc=0x100;unsigned physical=(in_user?0x30000:0x10000)+0x2000;c.load(physical,{0x55,0x66});auto before=c.registers();c.measure_traffic(true);instruction(c,post(store,2));captured(c,before,2);check(ps(c).frames.back().memory[3]==(in_user?7:0) && ps(c).frames.back().memory[10]==6 && !c.step_traffic().ram_writes && c.peek(physical)==0x55,"post preserves precise U/K rights failures before any transfer");}
 auto c=machine();c.registers().r[0]=0xabcd;c.registers().r[4]=0x100;instruction(c,post(true,2));check(!c.context() && c.registers().r[4]==0x102 && c.registers().pc==0x103 && c.peek(0x100)==0xcd && c.peek(0x101)==0xab,"post self-modifying store samples complete old encoding before commit");
 auto seed=machine();run(seed,{0x26,17});auto frame=seed.context();for(bool decrement:{false,true})for(unsigned flags=0;flags<64;++flags)if(!(flags&16))for(unsigned value:{0u,1u,0x7fffu,0x8000u,0xffffu}){auto c=seed;c.registers().r[0]=value;c.registers().flags=flags;instruction(c,{0x1a,uint8_t(decrement?0xf4:0xf0)});check(c.context()==frame && (c.csr()&64) && (c.registers().flags&32)==(flags&32),"word INC/DEC preserves active protected frame and independent I/E");}
}

}
int main(){try{units();post_transfers();relative_calls();sign_extension();interrupt_boundaries();assembled_example();rights_and_overlap();std::cout<<checks<<" density instruction checks passed\n";}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
