#include "cpu.hpp"
#include <algorithm>
#include <iostream>
#include <stdexcept>
#include <tuple>
using namespace vcpu;
namespace {
unsigned checks=0;
void check(bool b,const char* s){++checks;if(!b)throw std::runtime_error(s);}
const ProtectedState& ps(const VirtualCPU& c){return *c.protected_state();}
auto regs(const Registers& r){return std::make_tuple(r.r,r.pc,r.cs,r.ds,r.ss,r.sp,r.sb,r.st,r.dp,r.flags);}
auto frame(const ProtectedFrame& f){return std::make_tuple(f.words,f.controls,f.origin,f.bootstrap,f.prior_depth,f.source,f.privilege,f.memory);}
auto authority(const ProtectedFrame& f){return std::make_tuple(f.words[17],f.words[18],f.words[19],f.words[20],f.origin,f.bootstrap,f.prior_depth,f.source,f.privilege,f.memory);}
auto controls(const ProtectedState& p){return std::make_tuple(p.controls,p.descriptors,p.published,p.desc_map,p.desc_segment,p.shadow,p.entry_shadow,p.entry,p.entry_configured,p.query_address,p.query_run,p.query_kind,p.query_valid);}
void run(VirtualCPU& c,std::vector<uint8_t> b,unsigned pc=0x700){c.load(pc,b);c.registers().pc=pc;check(c.step(),"instruction advanced");}
void setc(VirtualCPU& c,unsigned id,unsigned v){c.registers().r[7]=v;run(c,{0x1f,0xb7,uint8_t(id)});check(!c.context(),"scalar control setup");}
void desc(VirtualCPU& c,unsigned map,unsigned seg,std::array<uint16_t,7> d){setc(c,0x20,map);setc(c,0x21,seg);for(unsigned i=0;i<7;++i)setc(c,0x22+i,d[i]);setc(c,0x7f,1);}
void setx(VirtualCPU& c,unsigned id,unsigned v){c.registers().r[7]=v;run(c,id<32?std::vector<uint8_t>{0x1f,uint8_t(0x40+id),7}:std::vector<uint8_t>{0x1f,0x97,uint8_t(id)});}
VirtualCPU machine(MachineProfile profile=MachineProfile::MinimalV1){VirtualCPU c(0x80000,profile);for(unsigned i=0;i<vcpu::exception_vector_count;++i)c.load(4*i,{0,3,0,0});c.load(0,{0,1,0,0});c.load(0x300,{0x1f,0x12});c.reset();for(unsigned i=0;i<5;++i)setc(c,0x10+i,i<3?0:0xfffe);setc(c,0x7f,5);
 desc(c,0,3,{3,0xc1,0,0,1,0,0});desc(c,7,1,{1,0x21,0x100,0x200,0,0,0});desc(c,7,2,{5,0x1d,0x1000,0x2000,0,0x1000,0x2000});desc(c,7,3,{3,0x19,0,0,1,0,0});setc(c,0x7f,3);c.load(0x10100,{0x26,17});return c;}
using Image=std::array<uint16_t,20>;
Image payload(const ProtectedFrame& f){Image a;std::copy_n(f.words.begin(),17,a.begin());std::copy(f.controls.begin(),f.controls.end(),a.begin()+17);return a;}
Image user_image(){Image a{};for(unsigned i=0;i<8;++i)a[i]=0x100+i;a[8]=0x100;a[9]=1;a[10]=32;a[11]=0x2000;a[12]=0x1000;a[13]=0x2000;a[14]=2;a[15]=0x1234;a[16]=3;a[17]=7;return a;}
std::vector<uint8_t> bytes(const Image& a){std::vector<uint8_t>b;for(auto w:a){b.push_back(uint8_t(w));b.push_back(uint8_t(w>>8));}return b;}
std::vector<uint8_t> memory(const VirtualCPU& c,unsigned address=0x32001){std::vector<uint8_t>b;for(unsigned i=0;i<40;++i)b.push_back(c.peek(address+i));return b;}
void load(VirtualCPU& c,const Image& a,unsigned address=0x32001){c.load(address,bytes(a));c.registers().ds=3;c.registers().r[4]=uint16_t(address);}
VirtualCPU active(bool user=false,bool bootstrap=false,MachineProfile profile=MachineProfile::MinimalV1){auto c=machine(profile);run(c,{0x26,9});if(user || bootstrap){auto a=user_image();for(unsigned i=0;i<17;++i)setx(c,i,a[i]);for(unsigned i=0;i<3;++i)setx(c,32+i,a[17+i]);c.registers().r[7]=7;run(c,{0x1f,0xb7,0x7f});if(user){run(c,{0x1f,0x12});check(c.mode()==Mode::User,"bootstrap entered U");c.step();check(c.mode()==Mode::Kernel && ps(c).frames.size()==1,"U trap frame");}}return c;}
void failed(VirtualCPU& c,const ProtectedFrame& old,const Registers& before,Cause cause,unsigned depth=1){check(ps(c).frames.size()==depth+1 && c.context()==ps(c).frames.back().words,"fault captured synchronized new frame");check(frame(ps(c).frames[depth-1])==frame(old),"old target frame survives byte-for-byte below fault");auto& f=ps(c).frames.back();check(f.words[17]==unsigned(cause) && f.words[8]==before.pc && std::equal(before.r.begin(),before.r.end(),f.words.begin()),"precise live handler PC/register capture");}
void success(){
 for(bool u:{false,true})for(bool boot:{false,true})if(!u || !boot)for(unsigned base=0;base<4;++base)for(unsigned flags=0;flags<64;++flags)if(!(flags&16))for(bool store:{false,true}){
  auto c=active(u,boot);auto candidate=u || boot?user_image():payload(ps(c).frames.back());for(unsigned i=0;i<8;++i)candidate[i]=0xa000+i;candidate[15]=0xbeef;
  load(c,candidate);c.registers().r={1,2,3,4,5,6,7,8};c.registers().r[4+base]=0x2001;c.registers().flags=flags;c.registers().pc=0x700;auto live=c.registers();auto f=ps(c).frames.back();auto state=controls(ps(c));auto time=ps(c).time;c.measure_traffic(true);
  run(c,{0x1f,uint8_t((store?0xcc:0xc8)+base)});auto expected=live;expected.pc+=2;
  check(regs(c.registers())==regs(expected) && controls(ps(c))==state,"bulk preserves all live registers/flags/control scratch");
  check(authority(ps(c).frames.back())==authority(f) && ps(c).frames.size()==1 && c.context()==ps(c).frames.back().words,"authority/metadata/E/depth preserved and public context synchronized");
  check(store?frame(ps(c).frames.back())==frame(f):payload(ps(c).frames.back())==candidate,"all twenty fields saved/loaded exactly");
  check(memory(c)==bytes(store?payload(f):candidate),"exact image and no source writes");check(ps(c).time==time+1 && c.step_traffic().ram_reads==(store?2u:42u) && c.step_traffic().ram_writes==(store?40u:0u),"one retirement and bounded RAM work (target probe does not read)");
 }
 // A nested K frame preserves controls and authority while validating under K.
 auto c=active(true);run(c,{0x26,55});auto lower=ps(c).frames.front();auto a=payload(ps(c).frames.back());a[0]=123;load(c,a);run(c,{0x1f,0xc8});check(ps(c).frames.size()==2 && frame(ps(c).frames.front())==frame(lower) && payload(ps(c).frames.back())==a,"nested K image replacement retains lower frame");
 run(c,{0x1f,0x12});check(ps(c).frames.size()==1 && c.registers().r[0]==123 && c.mode()==Mode::Kernel,"IRET independently restores nested K image");
}
void memory_faults(){auto seed=active(true);
 for(bool store:{false,true})for(unsigned bad=0;bad<40;++bad){auto c=seed;auto a=user_image();a[10]=0xffff;load(c,a);c.map(0x32001+bad,1,false);c.registers().pc=0x700;auto live=c.registers();auto old=ps(c).frames.back();auto source=memory(c);run(c,{0x1f,uint8_t(store?0xcc:0xc8)});failed(c,old,live,Cause::Memory);auto& f=ps(c).frames.back();check(f.memory==std::array<uint16_t,11>{2,2,uint16_t(store?3:2),0,3,uint16_t(0x2001+2*(bad/2)),0,uint16_t(0x2001+bad),0x103,2,8},"every missing byte width-two exact diagnostic precedes candidate validation");check(memory(c)==source && !ps(c).device_commits,"no partial output/source/MMIO effect");}
 for(bool store:{false,true})for(unsigned address:{0xffd7u,0xffd8u,0xffd9u,0xffdau,0xffffu}){auto c=seed;c.registers().ds=3;c.registers().r[4]=address;c.registers().pc=0x700;auto live=c.registers();auto old=ps(c).frames.back();if(address<=0xffd8){c.load(0x30000+address,bytes(user_image()));run(c,{0x1f,uint8_t(store?0xcc:0xc8)});check(ps(c).frames.size()==1,"odd/exact-fit full CTX span");}else{run(c,{0x1f,uint8_t(store?0xcc:0xc8)});failed(c,old,live,Cause::Memory);auto& m=ps(c).frames.back().memory;unsigned start=(address&1)?65535:65536;check(m[5]==uint16_t(start) && m[6]==(start>>16) && m[9]==2 && m[10]==1,"whole-span range diagnoses first nonfitting word");}}
 for(bool store:{false,true})for(unsigned flags:{0x41u,0x81u,0xc3u}){auto c=machine();desc(c,0,3,{3,uint16_t(flags),0,0,1,0,0});run(c,{0x26,0});load(c,payload(ps(c).frames.back()));auto old=ps(c).frames.back();c.registers().pc=0x700;auto live=c.registers();run(c,{0x1f,uint8_t(store?0xcc:0xc8)});bool allowed=store?flags==0x81:flags==0x41;if(allowed)check(ps(c).frames.size()==1,"STX write-only and LDX read-only RAM");else{failed(c,old,live,Cause::Memory);check(!ps(c).device_commits && ps(c).frames.back().memory[10]==(flags==0xc3?7:6),"kind/rights failure no MMIO");}}
 auto c=machine();desc(c,0,3,{255,0xc1,0,0,1,0,0});run(c,{0x26,0});c.registers().ds=3;c.registers().r[4]=0;auto old=ps(c).frames.back();c.registers().pc=0x700;auto live=c.registers();run(c,{0x1f,0xcc});failed(c,old,live,Cause::Memory);check(ps(c).frames.back().memory[10]==7 && !ps(c).device_commits,"actual MMIO behind RAM descriptor rejected");
}
void invalid_images(){auto seed=active(true);std::vector<std::pair<Image,Cause>> cases;
 for(auto [i,v]:std::vector<std::pair<unsigned,unsigned>>{{9,256},{14,256},{16,256},{10,0x40},{10,0},{17,0},{17,8},{17,256},{18,256},{19,256},{18,7},{19,1}}){auto a=user_image();a[i]=v;cases.push_back({a,Cause::Invalid});}
 for(auto [i,v]:std::vector<std::pair<unsigned,unsigned>>{{11,0xfff},{12,0x1001},{13,0x2001}}){auto a=user_image();a[i]=v;cases.push_back({a,Cause::Stack});}
 for(auto [i,v]:std::vector<std::pair<unsigned,unsigned>>{{16,4},{14,3},{8,0x200},{9,3}}){auto a=user_image();a[i]=v;cases.push_back({a,Cause::Memory});}
 for(auto [a,cause]:cases){auto c=seed;load(c,a);c.registers().pc=0x700;auto live=c.registers();auto old=ps(c).frames.back();run(c,{0x1f,0xc8});failed(c,old,live,cause);check(memory(c)==bytes(a),"invalid input unchanged");}
 // STX must export temporarily invalid scalar edits; LDX cannot import them.
 auto c=seed;setx(c,10,0xffff);auto invalid=ps(c).frames.back();load(c,Image{});run(c,{0x1f,0xcc});check(memory(c)==bytes(payload(invalid)),"STX exports inconsistent saved image without validation");auto live=c.registers();live.pc=0x700;run(c,{0x1f,0xc8});failed(c,invalid,live,Cause::Invalid);
 // Map invalidation between save/load invalidates a previously valid image.
 c=machine();setc(c,0x20,7);setc(c,0x7f,4);run(c,{0x26,0});load(c,user_image());auto old=ps(c).frames.back();live=c.registers();live.pc=0x700;run(c,{0x1f,0xc8});failed(c,old,live,Cause::Invalid);
}
void authority_and_fault_priority(){
 for(bool store:{false,true}){auto c=machine();c.registers().ds=255;c.registers().r[4]=0xffff;c.registers().pc=0x700;run(c,{0x1f,uint8_t(store?0xcc:0xc8)});check(ps(c).frames.size()==1 && ps(c).frames.back().words[17]==4,"inactive frame beats invalid pointer/MMIO");}
 for(bool store:{false,true}){auto c=active(true);run(c,{0x1f,0x12});check(c.mode()==Mode::User,"return to U for privilege");c.registers().r[4]=0xffff;c.load(0x10102,{0x1f,uint8_t(store?0xcc:0xc8)});c.step();check(ps(c).frames.back().words[17]==7 && ps(c).frames.back().privilege==(store?5:6) && ps(c).frames.back().memory==std::array<uint16_t,11>{},"U rejects context image before operands");}
 // K origin cannot acquire U bootstrap authority from a user-looking image.
 auto c=active();auto a=user_image();load(c,a);auto auth=authority(ps(c).frames.back());run(c,{0x1f,0xc8});check(authority(ps(c).frames.back())==auth && !ps(c).frames.back().bootstrap,"LDX does not grant bootstrap");run(c,{0x1f,0x12});check(c.mode()==Mode::Kernel,"image bits cannot promote a K frame to U return authority");
 // U origin always validates as U, even if RAM looks like a K image.
 c=active(true);a=user_image();a[17]=0;load(c,a);auto old=ps(c).frames.back();auto live=c.registers();live.pc=0x700;run(c,{0x1f,0xc8});failed(c,old,live,Cause::Invalid);
 for(bool store:{false,true})for(bool bad_target:{false,true}){c=active(true);a=user_image();if(bad_target)a[8]=0x200;load(c,a);old=ps(c).frames.back();auto source=memory(c);live=c.registers();live.pc=0xfffe;run(c,{0x1f,uint8_t(store?0xcc:0xc8)},0xfffe);failed(c,old,live,Cause::Memory);check(ps(c).frames.back().memory[1]==(!store && bad_target?6:3) && memory(c)==source,"candidate target precedes final-PC check; neither output nor frame commits");}
 // STX may overwrite its fetched code; the entire snapshot is from the old frame.
 c=active();a=payload(ps(c).frames.back());c.registers().ds=0;c.registers().r[4]=0x6fc;run(c,{0x1f,0xcc});check(memory(c,0x6fc)==bytes(a) && c.registers().pc==0x702,"STX code/header alias and next byte not fetched");
 c=active(true);load(c,user_image());c.map(0xffff,1,false);run(c,{0x1f,0xc8},0xfffd);check(ps(c).frames.size()==1 && c.registers().pc==0xffff,"LDX validates saved target separately without fetching next handler byte");
 // IRET must revalidate even after LDX succeeded.
 c=active(true);load(c,user_image());run(c,{0x1f,0xc8});c.map(0x10100,1,false);old=ps(c).frames.back();live=c.registers();live.pc=0x700;run(c,{0x1f,0x12});failed(c,old,live,Cause::Memory);check(ps(c).frames.back().memory[1]==6,"IRET retains target validation after LDX");
 for(unsigned depth:{7u,8u}){c=active();for(unsigned i=1;i<depth;++i)run(c,{0x26,0});auto frames=ps(c).frames;load(c,Image{});c.map(0x32028,1,false);run(c,{0x1f,0xc8});check(depth==7?ps(c).frames.size()==8:c.state()==ExecutionState::TerminalDoubleFault && ps(c).frames.size()==8,"nested capacity behavior unchanged");for(unsigned i=0;i<depth;++i)check(frame(ps(c).frames[i])==frame(frames[i]),"all lower frames unchanged on nested fault/capacity");}
}
void all_bulk_privilege_and_fetch_priority(){
 for(auto profile:{MachineProfile::MinimalV1,MachineProfile::ConsoleV1}){
  auto seed=active(true,false,profile);run(seed,{0x1f,0x12});
  check(seed.mode()==Mode::User,"prepare U-origin bulk privilege audit");
  for(unsigned tag=0xc0;tag<=0xcf;++tag)for(bool truncated:{false,true}){
   auto c=seed;c.registers().pc=0x100;c.registers().ds=255;c.registers().r[4+(tag&3)]=0xffff;
   c.load(0x10100,{0x1f,uint8_t(tag)});if(truncated)c.map(0x10101,1,false);
   auto before=c.registers();auto time=ps(c).time;auto descriptors=ps(c).descriptors;
   c.measure_traffic(true);check(c.step(),"raw bulk opcode progressed");auto& f=ps(c).frames.back();
   unsigned expected_class=tag<0xc4?9:tag<0xc8?8:tag<0xcc?6:5;
   check(f.words[17]==unsigned(truncated?Cause::Memory:Cause::Privilege) && f.words[8]==before.pc &&
         std::equal(before.r.begin(),before.r.end(),f.words.begin()),"all sixteen raw tags preserve U inputs and fetch-before-privilege priority");
   check(truncated?(f.memory[1]==1 && f.memory[9]==1 && !f.privilege):
         (f.privilege==expected_class && f.memory==std::array<uint16_t,11>{}),"exact operation privilege class or secondary-byte fetch diagnostics");
   check(ps(c).time==time && ps(c).descriptors==descriptors && c.step_traffic().ram_writes==0 &&
         c.step_traffic().fetch_reads==(truncated?1u:2u) && c.step_traffic().ram_reads==(truncated?5u:6u) &&
         c.step_traffic().mmio_samples==0 && !ps(c).device_commits,"U rejection never reads the overflowing image or publishes bulk effects");
  }
 }
}
void all_fields_and_irqs(){
 auto c=active();Image a{0x1100,0x2200,0x3300,0x4400,0x5500,0x6600,0x7700,0x8800,0x120,4,0x2f,0x2100,0x2000,0x2200,5,0xba98,6,7,7,3};
 load(c,a);run(c,{0x1f,0xc8});check(payload(ps(c).frames.back())==a,"distinct values in all twenty fields including K saved access controls");run(c,{0x1f,0xcc});check(memory(c)==bytes(a),"all twenty words serialized in selector order");run(c,{0x1f,0x12});check(c.mode()==Mode::Kernel && ps(c).frames.empty() && c.registers().pc==0x120 && c.registers().ds==6 && c.registers().ss==5 && c.registers().sp==0x2100 && ps(c).controls==std::array<uint16_t,3>{7,7,3},"K IRET applies loaded fields and controls separately");
 for(bool store:{false,true})for(bool masked:{false,true}) {
  c=active(true);load(c,user_image());auto old=ps(c).frames.back();c.registers().ds=255;c.registers().r[0]=2;run(c,{0x15,0x50,2,0});c.registers().ds=3;c.registers().r[4]=0x2001;c.registers().flags=masked?0:32;c.registers().pc=0x700;
  BoundaryEvent e{c.run(),c.boundary(),c.next_sequence(),BoundaryEvent::Kind::Edge};e.source=1;check(c.apply_boundary_events({e})==HostError::None,"queue surrounding IRQ");c.measure_traffic(true);run(c,{0x1f,uint8_t(store?0xcc:0xc8)});
  if(!masked)check(ps(c).frames.size()==2 && ps(c).frames.back().source==1 && frame(ps(c).frames.front())==frame(old) && c.step_traffic().fetch_reads==0,"eligible IRQ wins before bulk instruction");
  else {check(ps(c).frames.size()==1 && ps(c).pending==2 && c.registers().pc==0x702,"masked IRQ cannot split bulk commit");auto committed=ps(c).frames.front();c.registers().flags=32;c.step();check(ps(c).frames.size()==2 && frame(ps(c).frames.front())==frame(committed) && ps(c).frames.back().words[8]==0x702,"following IRQ sees complete frame and next PC");}
 }
}
}
int main(){try{success();memory_faults();invalid_images();authority_and_fault_priority();all_bulk_privilege_and_fetch_priority();all_fields_and_irqs();std::cout<<checks<<" context transfer checks passed\n";}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
