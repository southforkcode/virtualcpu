#include "bulk_guest.hpp"
#include <algorithm>
#include <fstream>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <tuple>
using namespace vcpu;
namespace {
unsigned checks=0;
void check(bool b,const char* text){++checks;if(!b)throw std::runtime_error(text);}
auto regs(const Registers& r){return std::tie(r.r,r.pc,r.cs,r.ds,r.ss,r.sp,r.sb,r.st,r.dp,r.flags);}
auto frame(const ProtectedFrame& f){return std::tie(f.words,f.controls,f.origin,f.bootstrap,f.prior_depth,f.source,f.privilege,f.memory);}
auto authority(const ProtectedFrame& f){return std::make_tuple(f.words[17],f.words[18],f.words[19],f.words[20],f.origin,f.bootstrap,f.prior_depth,f.source,f.privilege,f.memory);}
auto fault(const Fault& f){return std::tie(f.cause,f.address,f.memory,f.privilege);}
const ProtectedState& ps(const VirtualCPU& c){return *c.protected_state();}
bool same(const VirtualCPU& a,const VirtualCPU& b){
    if(a.state()!=b.state() || a.run()!=b.run() || a.boundary()!=b.boundary() || a.next_sequence()!=b.next_sequence() ||
       a.profile()!=b.profile() || a.csr()!=b.csr() || a.context()!=b.context() || regs(a.registers())!=regs(b.registers()) ||
       bool(a.diagnostic())!=bool(b.diagnostic()) || a.terminal_diagnostic() || b.terminal_diagnostic())return false;
    if(a.diagnostic() && fault(*a.diagnostic())!=fault(*b.diagnostic()))return false;
    auto key=[](const ProtectedState& p){return std::tie(p.mode,p.controls,p.descriptors,p.published,p.desc_map,p.desc_segment,
        p.shadow,p.entry_shadow,p.entry,p.entry_configured,p.query_address,p.query_run,p.query_kind,p.query_valid,
        p.pending,p.enable,p.in_service,p.levels,p.time,p.deadline,p.period,p.shadow_deadline,p.shadow_period,p.snapshot,
        p.timer_mode,p.shadow_mode,p.snapshot_valid,p.deadline_overflow,p.device_commits,p.console_rx,p.console_tx);};
    if(key(ps(a))!=key(ps(b)) || ps(a).frames.size()!=ps(b).frames.size())return false;
    for(unsigned i=0;i<ps(a).frames.size();++i)if(frame(ps(a).frames[i])!=frame(ps(b).frames[i]))return false;
    return true;
}
std::vector<uint8_t> image(const ProtectedFrame& f){
    std::vector<uint8_t> b;
    for(unsigned i=0;i<20;++i){auto v=i<17?f.words[i]:f.controls[i-17];b.push_back(uint8_t(v));b.push_back(uint8_t(v>>8));}
    return b;
}
std::string example(MachineProfile profile){
    using namespace bulk_images;
    auto c=bulk_guest::machine(profile),replay=bulk_guest::machine(profile);
    auto header=c.replay_header();std::vector<uint8_t> ram(1u<<24),backing(1u<<24);
    for(unsigned i=0;i<0x80000;++i){ram[i]=c.peek(i);backing[i]=1;}
    std::fill(backing.begin()+0xff0000,backing.begin()+0xff0100,2);
    check(replay.verify_replay_header(header,ram,backing)==HostError::None,"example binds replay to exact reset artifacts");
    ram[boot_origin]^=1;
    check(replay.verify_replay_header(header,ram,backing)==HostError::BadProfile,"changed loader code fails artifact binding");
    c.measure_traffic(true);replay.measure_traffic(true);
    unsigned boundaries=0,max_depth=0,attempts=0,transfers=0;ProtectedFrame original;
    while(c.state()==ExecutionState::Running && boundaries<1000){
        auto before=c.registers();auto frames=ps(c).frames;auto time=ps(c).time;
        bool loading=before.pc==load_load_context && !frames.empty();
        if(loading && !attempts)original=frames.front();
        if(frames.size()==2)check(frame(frames.front())==frame(original),"nested recovery leaves original frame and all authority untouched");
        check(c.step()==replay.step() && same(c,replay),"bootable bulk example replays every retained field at every boundary");
        ++boundaries;max_depth=std::max(max_depth,unsigned(ps(c).frames.size()));
        bool bulk=before.pc==boot_install || before.pc==boot_readback || before.pc==context_save_original ||
                  before.pc==context_save_candidate || loading;
        if(!bulk)continue;
        if(loading && ++attempts==1){
            check(ps(c).frames.size()==2 && ps(c).frames.back().words[17]==4 && ps(c).frames.back().words[8]==load_load_context &&
                  ps(c).frames.back().origin==Mode::Kernel && ps(c).frames.back().memory==std::array<uint16_t,11>{},"deliberate invalid CSR has precise nested Invalid State metadata");
            check(frame(ps(c).frames.front())==frame(original) && ps(c).time==time && !c.step_traffic().ram_writes,
                  "invalid LDX changes neither lower frame nor RAM and does not retire");
        }else{
            auto expected=before;expected.pc+=2;
            check(regs(c.registers())==regs(expected) && ps(c).time==time+1 && !c.step_traffic().mmio_samples && !ps(c).device_commits,
                  "bulk commit preserves live registers/flags and retires exactly once without MMIO");
            ++transfers;
            if(before.pc==context_save_original || before.pc==context_save_candidate){
                auto b=image(frames.back());unsigned address=before.pc==context_save_original?0x4001:0x4101;
                for(unsigned i=0;i<b.size();++i)check(c.peek(address+i)==b[i],"STX exports exact twenty-word little-endian image");
                check(frame(ps(c).frames.back())==frame(frames.back()) && c.step_traffic().ram_writes==40,"STX preserves frame and writes exact fixed span");
            }
            if(loading)check(ps(c).frames.size()==1 && ps(c).frames.front().words[0]==0xbeef &&
                authority(ps(c).frames.front())==authority(original) && c.context()==ps(c).frames.front().words,
                "repaired LDX retries and replaces payload without changing authority or depth");
        }
    }
    check(bulk_guest::finished(c) && same(c,replay) && max_depth==2 && attempts==2 && transfers==5,"complete descriptor/context example succeeds after exactly one registered retry");
    check(ps(c).controls==std::array<uint16_t,3>{} && ps(c).desc_map==0 && ps(c).desc_segment==0 && ps(c).shadow==std::array<uint16_t,7>{},
          "direct descriptor/context images leave shared staging and live association untouched");
    for(unsigned i=0;i<40;++i)if(i>=2)check(c.peek(0x4001+i)==c.peek(0x4101+i),"repair retains original context except explicitly replaced r0");
    for(unsigned i=0;i<0x80000;++i)check(c.peek(i)==replay.peek(i),"full final loader RAM matches replay");
    std::ostringstream out;
    out<<"{\"profile\":\""<<(profile==MachineProfile::MinimalV1?"minimal-v1":"console-v1")<<"\",\"replay_header\":"<<header
       <<",\"events\":[],\"boundaries\":"<<boundaries<<",\"retired\":"<<ps(c).time
       <<",\"successful_bulk_transfers\":"<<transfers<<",\"ldx_attempts\":"<<attempts<<",\"maximum_frame_depth\":"<<max_depth<<"}";
    return out.str();
}
}
int main(int argc,char** argv){
    try{
        auto a=example(MachineProfile::MinimalV1),b=example(MachineProfile::ConsoleV1);
        if(argc==2){std::ofstream out(argv[1]);out<<"{\"artifact_manifest_sha256\":\""<<bulk_images::manifest_sha256
            <<"\",\"checks\":"<<checks<<",\"programs\":["<<a<<','<<b<<"]}\n";if(!out)throw std::runtime_error("cannot write bulk execution evidence");}
        std::cout<<checks<<" bulk integration checks passed\n";
    }catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
}
