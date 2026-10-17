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
void instruction(VirtualCPU& c,std::vector<uint8_t> code,unsigned pc=0x700,unsigned cs=0) {
    auto d=ps(c).descriptors.at(uint16_t(cs));c.load((unsigned(d[0])<<16)|pc,code);c.registers().pc=pc;c.registers().cs=cs;check(c.step(),"instruction advanced");
}
void setc(VirtualCPU& c,unsigned selector,unsigned value){c.registers().r[7]=value;instruction(c,{0x1f,0xb7,uint8_t(selector)});check(!c.context(),"scalar setup");}
void descriptor(VirtualCPU& c,unsigned map,unsigned seg,std::array<uint16_t,7> d){setc(c,0x20,map);setc(c,0x21,seg);for(unsigned i=0;i<7;++i)setc(c,0x22+i,d[i]);setc(c,0x7f,1);}
VirtualCPU machine(){VirtualCPU c(0x80000);for(unsigned i=0;i<vcpu::exception_vector_count;++i)c.load(4*i,{uint8_t(0),uint8_t(i?3:1),0,0});c.load(0x300,{0x1f,0x12});c.reset();for(unsigned i=0;i<5;++i)setc(c,0x10+i,i<3?0:0xfffe);setc(c,0x7f,5);descriptor(c,0,3,{3,0xc1,0,0,1,0,0});return c;}
std::vector<uint8_t> words(std::array<uint16_t,9> a){std::vector<uint8_t>b;for(auto w:a){b.push_back(uint8_t(w));b.push_back(uint8_t(w>>8));}return b;}
const std::array<uint16_t,9> valid{7,3,3,0x19,0,0,1,0,0};
std::vector<uint8_t> image(const VirtualCPU& c,unsigned a=0x32001){std::vector<uint8_t>b;for(unsigned i=0;i<18;++i)b.push_back(c.peek(a+i));return b;}
auto scratch(const ProtectedState& p){return std::make_tuple(p.controls,p.published,p.desc_map,p.desc_segment,p.shadow,p.entry_shadow,p.entry,p.entry_configured,p.query_address,p.query_kind,p.query_run,p.query_valid);}
void fault(const VirtualCPU& c,const Registers& before,Cause cause,unsigned depth=0){
 check(c.state()==ExecutionState::Running && ps(c).frames.size()==depth+1,"ordinary nested fault entry");auto& f=ps(c).frames.back();
 check(f.words[17]==unsigned(cause) && f.words[8]==before.pc && f.words[9]==before.cs && f.words[10]==before.flags,"precise fault PC/CS/flags/cause");
 check(std::equal(before.r.begin(),before.r.end(),f.words.begin()) && f.words[11]==before.sp && f.words[14]==before.ss && f.words[16]==before.ds,"fault captures all old register state");
}
void success() {
 auto seed=machine();descriptor(seed,8,3,{4,0x19,0,0,1,0,0});setc(seed,0x7f,3);setc(seed,0,8);setc(seed,1,8);setc(seed,2,3);
 setc(seed,0x20,11);setc(seed,0x21,9);for(unsigned i=0;i<7;++i)setc(seed,0x22+i,std::array<uint16_t,7>{6,0x19,0x3000,0x2000,0,0,0}[i]);setc(seed,0x40,0xfff0);setc(seed,0x41,7);setc(seed,0x7f,8);
 for(bool active:{false,true})for(unsigned flags=0;flags<64;++flags)if(!(flags&0x10))for(unsigned base=0;base<4;++base)for(bool store:{false,true}) {
  auto c=seed;
  if(store){
   auto record=words(valid);c.load(0x32001,record);c.registers().ds=3;c.registers().r[4]=0x2001;instruction(c,{0x1f,0xc0});
  }
  if(active)instruction(c,{0x26,0});
  c.registers().ds=3;c.registers().r={0x1234,0x5678,0x9abc,0xdef0,1,2,3,4};c.registers().r[4+base]=0x2001;c.registers().flags=uint16_t(flags&0x2f);
  auto record=valid;if(store)std::fill(record.begin()+2,record.end(),0xffff);c.load(0x32001,words(record));
  auto before=c.registers();auto saved=scratch(ps(c));auto context=c.context();auto frames=ps(c).frames;auto time=ps(c).time;c.measure_traffic(true);
  instruction(c,{0x1f,uint8_t((store?0xc4:0xc0)+base)});
  check(c.registers().r==before.r && c.registers().flags==before.flags && c.registers().sp==before.sp && c.registers().ds==3 && c.registers().pc==0x702,"preserved GPR/flags/pointer/segments/SP and next PC");
  check(scratch(ps(c))==saved && c.context()==context && ps(c).frames.size()==frames.size(),"all shared staging/query/live controls/publication and frame unchanged");
  check(ps(c).descriptors.at(0x703)==std::array<uint16_t,7>{3,0x19,0,0,1,0,0} && !ps(c).published[7],"committed descriptor without map publication");
  check(image(c)==words(valid) && ps(c).time==time+1,"snapshot/readback image and exactly one retirement");
  check(c.step_traffic().ram_reads==(store?6u:20u) && c.step_traffic().ram_writes==(store?18u:0u) && !ps(c).device_commits,"exact payload RAM traffic; no MMIO");
 }
 for(unsigned key:{0u,255u}) {auto c=machine();auto d=valid;d[0]=key;d[1]=255;d[2]=0;d[3]=0;d[6]=0;c.load(0x32001,words(d));c.registers().ds=3;c.registers().r[7]=0x2001;instruction(c,{0x1f,0xc3});check(ps(c).descriptors.at(uint16_t((key<<8)|255))==std::array<uint16_t,7>{},"canonical absent entry endpoints");}
 auto absent=machine();auto d=valid;d[0]=255;d[1]=255;std::fill(d.begin()+2,d.end(),0xffff);absent.load(0x32001,words(d));absent.registers().ds=3;absent.registers().r[4]=0x2001;instruction(absent,{0x1f,0xc4});std::fill(d.begin()+2,d.end(),0);check(image(absent)==words(d) && !ps(absent).descriptors.count(0xffff),"missing readback is zeros without insertion/publication");
}
void memory_failures() {
 for(bool store:{false,true})for(unsigned bad=0;bad<18;++bad)for(bool malformed:{false,true}) {
  auto c=machine();auto d=valid;if(malformed)d[0]=256;c.load(0x32001,words(d));c.map(0x32001+bad,1,false);c.registers().ds=3;c.registers().r[4]=0x2001;c.registers().pc=0x700;
  auto before=c.registers();auto descs=ps(c).descriptors;auto saved=image(c);c.measure_traffic(true);instruction(c,{0x1f,uint8_t(store?0xc4:0xc0)});fault(c,before,Cause::Memory);
  auto& f=ps(c).frames.back();auto access=store && bad>=4?3:2;
  check(f.memory==std::array<uint16_t,11>{2,2,uint16_t(access),0,3,uint16_t(0x2001+2*(bad/2)),0,uint16_t(0x2001+bad),0x103,2,8} && f.words[18]==0x2001+bad && f.words[19]==0x103,"every failing byte gets exact width-two logical/physical metadata");
  check(ps(c).descriptors==descs && image(c)==saved && !c.step_traffic().ram_writes && !ps(c).device_commits,"late failure never publishes descriptor or any destination byte");
 }
 for(bool store:{false,true})for(unsigned base:{0xffeeu,0xffedu,0xffefu,0xfff0u,0xffffu}) {
  auto c=machine();c.registers().ds=3;c.registers().r[4]=base;c.registers().pc=0x700;auto before=c.registers();
  if(base<=0xffee){c.load(0x30000+base,words(valid));instruction(c,{0x1f,uint8_t(store?0xc4:0xc0)});check(!c.context(),"unaligned/exact-fit end succeeds");}
  else {descriptor(c,0,3,{});c.registers()=before;instruction(c,{0x1f,uint8_t(store?0xc4:0xc0)});fault(c,before,Cause::Memory);auto& m=ps(c).frames.back().memory;unsigned start=(base&1)?0xffff:0x10000;check(m[5]==uint16_t(start) && m[6]==(start>>16) && m[9]==2 && m[10]==1 && m[8]==(start<=65535?0x103:0),"whole-image range precedes absent mapping");}
 }
 for(bool store:{false,true})for(auto d:std::vector<std::array<uint16_t,7>>{{3,0x81,0,0,1,0,0},{3,0x41,0,0,1,0,0},{255,0xc3,0,0,1,0,0},{255,0xc1,0,0,1,0,0},{3,0xc1,0,0x2012,0,0,0}}) {
  auto c=machine();descriptor(c,0,3,d);c.load(0x32001,words(valid));c.registers().ds=3;c.registers().r[4]=d[0]==255?0:0x2001;c.registers().pc=0x700;auto before=c.registers();c.measure_traffic(true);instruction(c,{0x1f,uint8_t(store?0xc4:0xc0)});
  if(!store && d[1]==0x41){check(!c.context(),"LDC needs read only");continue;}
  fault(c,before,Cause::Memory);auto& m=ps(c).frames.back().memory;
  check(m[10]==(d[0]==255?7:d[1]==0xc1?5:6) && m[9]==2 && !ps(c).device_commits && !c.step_traffic().mmio_samples,"RAM bounds/rights/device faults without callback");
 }
}
void invalid_and_final_pc() {
 std::vector<std::array<uint16_t,9>> bad;
 for(unsigned field:{0u,1u,2u}){auto d=valid;d[field]=256;bad.push_back(d);}
 for(auto payload:std::vector<std::array<uint16_t,7>>{{3,0x200,0,0,1,0,0},{1,0,0,0,0,0,0},{3,1,0,1,1,0,0},{3,1,2,1,0,0,0},{3,1,0,0,2,0,0},{3,1,0,0,1,1,0},{3,5,0,0x2000,0,0x3000,0x1000},{255,0x23,0,0,1,0,0},{255,0x107,0,0,1,0,0}}){auto d=valid;std::copy(payload.begin(),payload.end(),d.begin()+2);bad.push_back(d);}
 auto kernel=valid;kernel[0]=0;bad.push_back(kernel);
 for(auto d:bad){auto c=machine();c.load(0x32001,words(d));c.registers().ds=3;c.registers().r[4]=0x2001;c.registers().pc=0x700;auto before=c.registers();auto descs=ps(c).descriptors;instruction(c,{0x1f,0xc0});fault(c,before,Cause::Invalid);check(ps(c).descriptors==descs && image(c)==words(d) && ps(c).frames.back().memory==std::array<uint16_t,11>{},"invalid aggregate never installs or rewrites source");}
 for(unsigned field:{0u,1u}) {auto c=machine();auto d=valid;d[field]=256;c.load(0x32001,words(d));c.registers().ds=3;c.registers().r[4]=0x2001;c.registers().pc=0x700;auto before=c.registers();instruction(c,{0x1f,0xc4});fault(c,before,Cause::Invalid);check(image(c)==words(d),"invalid STC header leaves complete destination untouched");}
 for(bool store:{false,true})for(bool hole:{false,true}) {
  auto c=machine();c.load(0x32001,words(valid));if(hole)c.map(0x32012,1,false);c.registers().ds=3;c.registers().r[4]=0x2001;c.registers().pc=0xfffe;auto before=c.registers();auto descs=ps(c).descriptors;auto saved=image(c);c.measure_traffic(true);instruction(c,{0x1f,uint8_t(store?0xc4:0xc0)},0xfffe);fault(c,before,Cause::Memory);
  auto& m=ps(c).frames.back().memory;check(m[1]==(hole?2:3) && m[9]==(hole?2:1) && m[10]==(hole?8:1),"operand failure precedes final-PC failure");check(ps(c).descriptors==descs && image(c)==saved && !c.step_traffic().ram_writes,"failed final PC discards prepared insertion/write batch");
 }
}
void descriptor_kinds() {
 for(auto d:std::vector<std::array<uint16_t,9>>{{0,20,255,0xc3,0,256,0,0,0},{7,2,5,0x1d,0x1000,0x2000,0,0x1000,0x2000}}) {
  auto c=machine();c.load(0x32001,words(d));c.registers().ds=3;c.registers().r[4]=0x2001;instruction(c,{0x1f,0xc0});check(!c.context() && !ps(c).device_commits,"valid DEVICE/STACK candidate requires no backing/device probe of described region");instruction(c,{0x1f,0xc4});check(image(c)==words(d),"DEVICE/STACK descriptors read back through ordinary RAM");
 }
 auto c=machine();descriptor(c,7,3,{3,0x19,0,0,1,0,0});setc(c,0x7f,3);setc(c,0,7);setc(c,1,7);setc(c,2,3);auto d=valid;d[2]=6;c.load(0x32001,words(d));c.registers().ds=3;c.registers().r[4]=0x2001;instruction(c,{0x1f,0xc0});check(ps(c).published[7] && ps(c).controls==std::array<uint16_t,3>{7,7,3} && ps(c).descriptors.at(0x703)[0]==6,"published-map replacement preserves process/access association");
}
void aliases_and_depth() {
 auto c=machine();auto d=valid;d[0]=0;d[1]=3;d[2]=4;d[3]=0xc1;c.load(0x32001,words(d));c.registers().ds=3;c.registers().r[4]=0x2001;instruction(c,{0x1f,0xc0});check(ps(c).descriptors.at(3)[0]==4 && image(c)==words(d),"source DS replacement reads old mapping then commits whole descriptor");
 auto code=machine();descriptor(code,0,4,{1,0x1c1,0,0,1,0,0});d=valid;d[0]=0;d[1]=4;d[2]=6;d[3]=0xc1;code.load(0x32001,words(d));code.registers().ds=3;code.registers().r[4]=0x2001;instruction(code,{0x1f,0xc0},0x700,4);check(code.registers().pc==0x702 && !code.context(),"CS replacement commits without implicit following-byte fetch");code.step();check(ps(code).frames.back().words[8]==0x702 && ps(code).frames.back().memory[1]==1 && ps(code).frames.back().memory[10]==6,"next fetch uses replacement mapping");
 auto alias=machine();auto header=valid;alias.load(0x6fc,words(header));alias.registers().r[4]=0x6fc;instruction(alias,{0x1f,0xc4});auto zeros=header;std::fill(zeros.begin()+2,zeros.end(),0);check(image(alias,0x6fc)==words(zeros) && alias.registers().pc==0x702,"STC can overwrite already-fetched instruction payload atomically");
 for(unsigned depth:{7u,8u}) {
  auto nested=machine();for(unsigned i=0;i<depth;++i)instruction(nested,{0x26,0});auto frames=ps(nested).frames;nested.load(0x32001,words(valid));nested.map(0x32012,1,false);nested.registers().ds=3;nested.registers().r[4]=0x2001;nested.registers().pc=0x700;auto before=nested.registers();instruction(nested,{0x1f,0xc0});
  if(depth==7)fault(nested,before,Cause::Memory,7);else check(nested.state()==ExecutionState::TerminalDoubleFault && ps(nested).frames.size()==8 && nested.terminal_diagnostic()->reason=="capacity","depth-eight fault retains all frames");
  for(unsigned i=0;i<depth;++i)check(ps(nested).frames[i].words==frames[i].words && ps(nested).frames[i].controls==frames[i].controls,"lower frames unchanged after bulk fault");
 }
}
void decode_and_irq_boundaries() {
 for(bool store:{false,true}) {
  auto c=machine();c.load(0x32001,words(valid));c.registers().ds=3;c.registers().r[4]=0x2001;c.registers().pc=0xffff;auto before=c.registers();c.measure_traffic(true);
  instruction(c,{0x1f},0xffff);fault(c,before,Cause::Memory);
  check(ps(c).frames.back().memory[1]==1 && ps(c).frames.back().memory[9]==1 && !c.step_traffic().ram_writes && !ps(c).descriptors.count(0x703),"missing secondary byte is fetch failure before record access");
  auto reserved=machine();reserved.registers().pc=0xfffe;before=reserved.registers();instruction(reserved,{0x1f,0xd0},0xfffe);fault(reserved,before,Cause::Illegal);
  auto next=machine();next.load(0x32001,words(valid));next.map(0xffff,1,false);next.registers().ds=3;next.registers().r[4]=0x2001;instruction(next,{0x1f,uint8_t(store?0xc4:0xc0)},0xfffd);check(!next.context() && next.registers().pc==0xffff,"representable next PC does not fetch missing next byte");
 }
 auto c=machine();c.registers().ds=255;c.registers().r[0]=2;instruction(c,{0x15,0x50,2,0});c.registers().ds=3;c.registers().r[4]=0x2001;c.registers().pc=0x700;c.registers().flags=32;c.load(0x32001,words(valid));auto before=c.registers();
 BoundaryEvent edge{c.run(),c.boundary(),c.next_sequence(),BoundaryEvent::Kind::Edge};edge.source=1;check(c.apply_boundary_events({edge})==HostError::None,"queued IRQ");c.measure_traffic(true);instruction(c,{0x1f,0xc0});fault(c,before,Cause::IRQ);
 check(!ps(c).descriptors.count(0x703) && c.step_traffic().fetch_reads==0 && c.step_traffic().ram_reads==4 && !c.step_traffic().ram_writes,"eligible IRQ is accepted before atomic descriptor instruction");
}
void user_privilege() {
 for(bool store:{false,true}) {
  auto c=machine();descriptor(c,7,1,{1,0x21,0x100,0x200,0,0,0});descriptor(c,7,2,{5,0x1d,0x1000,0x2000,0,0x1000,0x2000});descriptor(c,7,3,{3,0x19,0,0,1,0,0});setc(c,0x7f,3);instruction(c,{0x26,0});
  auto setx=[&](unsigned id,unsigned v){c.registers().r[7]=v;instruction(c,id<32?std::vector<uint8_t>{0x1f,uint8_t(0x40+id),7}:std::vector<uint8_t>{0x1f,0x97,uint8_t(id)});};
  for(auto [id,v]:std::vector<std::pair<unsigned,unsigned>>{{8,0x100},{9,1},{10,32},{11,0x2000},{12,0x1000},{13,0x2000},{14,2},{16,3},{32,7},{33,0},{34,0}})setx(id,v);
  c.registers().r[7]=7;instruction(c,{0x1f,0xb7,0x7f});instruction(c,{0x1f,0x12});check(c.mode()==Mode::User,"bootstrap user");c.registers().r[4]=0xffff;c.load(0x10100,{0x1f,uint8_t(store?0xc4:0xc0)});auto before=c.registers();c.measure_traffic(true);c.step();fault(c,before,Cause::Privilege);check(ps(c).frames.back().privilege==(store?8:9) && ps(c).frames.back().memory==std::array<uint16_t,11>{} && c.step_traffic().ram_reads==6 && !c.step_traffic().ram_writes,"U privilege wins before invalid buffer, only fetch/vector reads occur");
 }
}
}
int main(){try{success();memory_failures();invalid_and_final_pc();descriptor_kinds();aliases_and_depth();decode_and_irq_boundaries();user_privilege();std::cout<<checks<<" descriptor transfer checks passed\n";}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
