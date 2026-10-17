#include "compiler_images.hpp"
#include "console_guest.hpp"
#include <algorithm>
#include <fstream>
#include <iostream>
#include <map>
#include <stdexcept>
#include <tuple>
using namespace vcpu;
namespace {
size_t checks=0;
void check(bool b,const std::string& s) { ++checks;if(!b)throw std::runtime_error(s); }
const ProtectedState& ps(const VirtualCPU& c){return *c.protected_state();}
unsigned word(const VirtualCPU& c,unsigned a){return console_guest::word(c,a);}
// Compare every retained CPU/device field, including normally invisible staging.
bool same_state(const VirtualCPU& c,const VirtualCPU& d) {
    auto regs=[](const Registers& r){return std::tie(r.r,r.pc,r.cs,r.ds,r.ss,r.sp,r.sb,r.st,r.dp,r.flags);};
    auto fault=[](const Fault& f){return std::tie(f.cause,f.address,f.memory,f.privilege);};
    if(std::make_tuple(c.state(),c.profile(),c.run(),c.boundary(),c.next_sequence(),c.csr(),c.context())!=
       std::make_tuple(d.state(),d.profile(),d.run(),d.boundary(),d.next_sequence(),d.csr(),d.context()) ||
       regs(c.registers())!=regs(d.registers()) || bool(c.diagnostic())!=bool(d.diagnostic()) ||
       bool(c.terminal_diagnostic())!=bool(d.terminal_diagnostic())) return false;
    if(c.diagnostic() && fault(*c.diagnostic())!=fault(*d.diagnostic())) return false;
    auto key=[](const ProtectedState& p){return std::tie(p.mode,p.controls,p.descriptors,p.published,p.desc_map,p.desc_segment,
        p.shadow,p.entry_shadow,p.entry,p.entry_configured,p.query_address,p.query_run,p.query_kind,p.query_valid,
        p.pending,p.enable,p.in_service,p.levels,p.time,p.deadline,p.period,p.shadow_deadline,p.shadow_period,p.snapshot,
        p.timer_mode,p.shadow_mode,p.snapshot_valid,p.deadline_overflow,p.device_commits,p.console_rx,p.console_tx);};
    auto& p=ps(c);auto& q=ps(d);
    if(key(p)!=key(q) || p.frames.size()!=q.frames.size()) return false;
    auto frame=[](const ProtectedFrame& f){return std::tie(f.words,f.controls,f.origin,f.bootstrap,f.prior_depth,f.source,f.privilege,f.memory);};
    for(size_t i=0;i<p.frames.size();++i) if(frame(p.frames[i])!=frame(q.frames[i])) return false;
    if(c.terminal_diagnostic()) {
        auto& a=*c.terminal_diagnostic();auto& b=*d.terminal_diagnostic();
        auto terminal=[](const TerminalDiagnostic& t){return std::tie(t.reason,t.stage,t.event,t.pc,t.service,t.source,t.depth,t.captured,t.mode,t.controls,t.pending,t.in_service);};
        if(terminal(a)!=terminal(b) || fault(a.failure)!=fault(b.failure) || regs(a.registers)!=regs(b.registers)) return false;
    }
    return true;
}
VirtualCPU machine(const compiler_images::Program& program) {
    using namespace kernel_images;
    VirtualCPU c(0x80000,MachineProfile::ConsoleV1);
    for(unsigned n=0;n<8;++n){unsigned t=n==0?0x100:n==5?console_irq:n==6?console_swi:0x1800;c.load(4*n,{uint8_t(t),uint8_t(t>>8),0,0});}
    c.load(0x100,program.helper.empty()?compiler_images::boot:compiler_images::helper_boot);c.load(0x8000,compiler_images::kernel);
    c.load(console_memory_origin,console_memory);c.load(load_context_origin,load_context);c.load(controls_origin,controls);
    c.load(control_fault_origin,control_fault);c.load(0xa000,control_registry);c.load(0x1400,{1});
    c.load(0x10100,program.code);c.load(0x20100,console_peer);c.load(0x32000,program.data);
    c.load(0xb000,program.helper);console_guest::word(c,0x32100,32);console_guest::word(c,0x32102,0x2000);console_guest::word(c,0x32104,3);
    c.load(0x30000,{0xa7});c.load(0x3ffff,{0x5c});console_guest::word(c,0x2f10,0x5aa5);console_guest::word(c,0x42f10,0xa55a);
    c.reset();return c;
}
struct Metrics {
    uint64_t retired=0,compiled=0,bank_moves=0,spills=0,reloads=0,address_alu=0,guards=0,branches=0,sp_accesses=0,b1_accesses=0;
    unsigned stack=0,kernel_stack=0;
    std::map<std::string,uint64_t> families;
    std::map<int,uint64_t> sp_offsets,b1_offsets,branch_distances;
    std::map<int,uint64_t> instruction_pcs;
};
void histogram(std::ostream& out,const std::map<int,uint64_t>& values){bool first=true;out<<'{';for(auto [k,v]:values){if(!first)out<<',';first=false;out<<'"'<<k<<"\":"<<v;}out<<'}';}
void run(const compiler_images::Program& p,std::ostream& report,bool first) {
    auto c=machine(p);auto replayed=c;auto header=c.replay_header();std::vector<std::string> events;
    std::map<unsigned,const compiler_images::Instruction*> user,kernel;
    for(auto& ins:p.instructions)user[ins.pc]=&ins;
    for(auto& ins:p.helper_instructions)kernel[ins.pc]=&ins;
    Metrics m;bool injected=false,seen_b=false,seen_ss_irq=false;unsigned steps=0,guard_pc=0;
    while(steps++<2000000) {
        if(!p.input.empty() && !injected && word(c,0x402a)==2 && word(c,0x42000)>=3 && c.mode()==Mode::User && ps(c).controls[0]==8) {
            auto event=console_guest::input(c,std::vector<uint8_t>(p.input.begin(),p.input.end()));events.push_back(event);
            check(c.apply_boundary_json(event)==HostError::None && replayed.apply_boundary_json(event)==HostError::None,p.name+": recorded input");injected=true;
        }
        if(c.mode()==Mode::User) {m.stack=std::max(m.stack,0x2000u-c.registers().sp);seen_b|=ps(c).controls[0]==8;}
        bool compiled_user=c.mode()==Mode::User && ps(c).controls[0]==7 && c.registers().cs==1;
        const auto& map=compiled_user?user:kernel;auto found=map.find(c.registers().pc);
        const auto* ins=(compiled_user || c.mode()==Mode::Kernel && c.registers().cs==0) && found!=map.end()?found->second:nullptr;
        auto time=ps(c).time;auto advanced=c.step();check(advanced==replayed.step() && same_state(c,replayed),p.name+": replay at boundary "+c.boundary());
        check(advanced,p.name+": halted/stalled unexpectedly");
        if(ps(c).time!=time) {
            ++m.retired;
            if(ins) {
                ++m.compiled;++m.families[ins->family];m.bank_moves+=ins->bank_move;m.spills+=ins->kind==3;m.reloads+=ins->kind==4;
                ++m.instruction_pcs[ins->pc];
                m.address_alu+=ins->address_alu;m.guards+=ins->kind==2;
                if(ins->addressing==1){++m.sp_accesses;++m.sp_offsets[ins->displacement];}
                if(ins->addressing==2){++m.b1_accesses;++m.b1_offsets[ins->displacement];}
                if(ins->branch!=100000){++m.branches;++m.branch_distances[ins->branch];}
                if(!compiled_user)m.kernel_stack=std::max(m.kernel_stack,0xfffeu-c.registers().sp);
                if(compiled_user && ins->family=="software_trap" && word(c,0x32f04))guard_pc=ins->pc;
            }
        }
        if(!ps(c).frames.empty() && ps(c).frames.front().source==0 && ps(c).frames.front().origin==Mode::User && ps(c).frames.front().words[16]==2)seen_ss_irq=true;
        if((c.state()==ExecutionState::Halted && c.registers().pc==compiler_images::finished_pc) || c.state()==ExecutionState::Halted && c.registers().pc==0x1401)break;
        check(c.state()==ExecutionState::Running && !c.terminal_diagnostic(),p.name+": unexpected terminal transition");
    }
    check(steps<2000000,p.name+": execution limit");
    uint64_t counted=0;for(auto [pc,count]:m.instruction_pcs)counted+=count;
    check(counted==m.compiled,p.name+": per-PC retirement accounting");
    if(p.guard) {
        check(c.registers().pc==0x1401 && word(c,0x32f04)==p.guard && ps(c).frames.size()==1 && ps(c).frames.back().words[17]==6 && ps(c).frames.back().words[20]==255 && guard_pc,p.name+": exact compiler pointer failure trap");
        check(c.peek(0x30000)==0xa7 && c.peek(0x3ffff)==0x5c,p.name+": failed checked address cannot wrap into another byte");
    } else {
        check(c.state()==ExecutionState::Halted && c.registers().pc==compiler_images::finished_pc && word(c,0x32f00)==p.result,p.name+": result expected "+std::to_string(p.result)+" got "+std::to_string(word(c,0x32f00)));
        auto& frame=ps(c).frames.back();check(frame.origin==Mode::User && frame.words[11]==0x2000 && frame.words[4]==0x2000 && frame.words[5]==0 && frame.words[6]==0 && frame.words[7]==0,p.name+": balanced stack and B1 callee saves");
        check(word(c,0x523a)==1 && word(c,0x5242)==1 && ps(c).controls[1]==0 && word(c,0x5000)==0,p.name+": only PCB lifetime leases retained");
    }
    check(word(c,0x2f00)==p.kernel_result && word(c,0x2f10)==0x5aa5 && word(c,0x42f10)==0xa55a,p.name+": user/kernel/peer memory isolation");
    if(p.unchanged)for(unsigned i=0;i<p.data.size();++i)check(c.peek(0x32000+i)==p.data[i],p.name+": invalid pointer preserves data");
    for(auto& expected:p.memory)for(unsigned i=0;i<expected.bytes.size();++i)check(c.peek(0x30000+expected.offset+i)==expected.bytes[i],p.name+": expected data at "+std::to_string(expected.offset+i));
    check(std::string(ps(c).console_tx.begin(),ps(c).console_tx.end())==p.output && ps(c).console_rx.empty(),p.name+": terminal output and RX consumption");
    if(!p.input.empty())check(injected && seen_b && word(c,0x300a)==1 && word(c,0x300c)==1,p.name+": generated terminal blocks and wakes alongside peer");
    if(p.preempt_ss)check(seen_ss_irq,p.name+": saved DS=SS window resumes correctly after preemption");
    if(p.preempt)check(seen_b && word(c,0x3006)>=2 && word(c,0x42000)>0,p.name+": compiler binary survives timer preemption");
    bool same=true;for(unsigned a=0;a<0x80000;++a)same&=c.peek(a)==replayed.peek(a);check(same,p.name+": final RAM replay");
    if(!first)report<<",\n";
    report<<"{\"name\":\""<<p.name<<"\",\"variant\":\""<<p.variant<<"\",\"static_bytes\":"<<p.code.size()+p.helper.size()<<",\"data_bytes\":"<<p.data.size()
      <<",\"retired_instructions\":"<<m.compiled<<",\"system_retired_instructions\":"<<m.retired<<",\"boundaries\":"<<c.boundary()
      <<",\"bank_moves\":"<<m.bank_moves<<",\"spills\":"<<m.spills<<",\"reloads\":"<<m.reloads<<",\"maximum_user_stack_bytes\":"<<m.stack
      <<",\"maximum_helper_stack_bytes\":"<<m.kernel_stack<<",\"address_alu\":"<<m.address_alu<<",\"checked_address_instructions\":"<<m.guards
      <<",\"ss_window_preempted\":"<<(seen_ss_irq?"true":"false")<<",\"guard_trap_pc\":"<<guard_pc<<",\"exit_csr\":"<<ps(c).frames.back().words[10]<<",\"peer_progress\":"<<word(c,0x42000)<<",\"schedule_selections\":"<<word(c,0x3006)
      <<",\"dynamic_sp_offsets\":";histogram(report,m.sp_offsets);report<<",\"dynamic_b1_offsets\":";histogram(report,m.b1_offsets);report<<",\"dynamic_branch_distances\":";histogram(report,m.branch_distances);
    report<<",\"dynamic_instruction_pcs\":";histogram(report,m.instruction_pcs);
    report<<",\"instruction_families\":{";bool f=true;for(auto& [family,count]:m.families){if(!f)report<<',';f=false;report<<'"'<<family<<"\":"<<count;}report<<"},\"replay_header\":"<<header<<",\"events\":[";
    for(size_t i=0;i<events.size();++i){if(i)report<<',';report<<events[i];}report<<"]}";
    std::cout<<p.name<<": "<<m.compiled<<" compiled instructions, "<<c.boundary()<<" B, stack="<<m.stack<<'\n';
}
}
int main(int argc,char** argv) {
    try {
        std::ofstream output;std::ostringstream discard;
        if(argc==2){output.open(argv[1]);check(bool(output),"report path");}else check(argc==1,"usage: compiler_conformance [report.json]");
        std::ostream& report=argc==2?static_cast<std::ostream&>(output):static_cast<std::ostream&>(discard);
        report<<"{\"artifact_manifest_sha256\":\""<<compiler_images::manifest_sha256<<"\",\"programs\":[\n";bool first=true;for(auto& p:compiler_images::programs){run(p,report,first);first=false;}
        report<<"\n],\"checks\":"<<(checks+1)<<"}\n";check(bool(report),"report write");std::cout<<checks<<" compiler execution checks passed\n";
    } catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
}
