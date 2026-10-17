#include "density_images.hpp"
#include "console_guest.hpp"
#include "workloads/density/census.hpp"
#include <chrono>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <tuple>
using namespace vcpu;
namespace {
void require(bool b,const std::string& message){if(!b)throw std::runtime_error(message);}
bool same_end(const VirtualCPU& a,const VirtualCPU& b) {
    auto regs=[](const Registers& r){return std::tie(r.r,r.pc,r.cs,r.ds,r.ss,r.sp,r.sb,r.st,r.dp,r.flags);};
    auto state=[](const ProtectedState& p){return std::tie(p.mode,p.controls,p.time,p.descriptors,p.published,p.shadow,p.desc_map,p.desc_segment,p.entry_shadow,p.entry,p.query_address,p.query_kind,p.query_run,p.query_valid,p.console_rx,p.console_tx,p.device_commits);};
    return regs(a.registers())==regs(b.registers()) && state(*a.protected_state())==state(*b.protected_state()) && a.context()==b.context() && a.state()==b.state() && a.boundary()==b.boundary();
}
VirtualCPU probe(const density_images::Program& p) {
    VirtualCPU c(0x80000);c.load(0,{uint8_t(p.origin),uint8_t(p.origin>>8),0,0});c.load(p.origin,p.code);
    if(p.controls){c.load(kernel_images::controls_origin,p.control_code.empty()?kernel_images::controls:p.control_code);c.load(0xa000,p.registry.empty()?kernel_images::control_registry:p.registry);}
    for(auto& [address,data]:p.data)c.load(address,data);
    c.reset();return c;
}
VirtualCPU minimal() {
    VirtualCPU c(0x80000);using namespace kernel_images;
    for(unsigned n=0;n<vcpu::exception_vector_count;++n){unsigned t=n==0?0x100:n==5?0x1000:n==6?0x1200:0x1800;c.load(4*n,{uint8_t(t),uint8_t(t>>8),0,0});}
    for(auto& image:std::vector<std::pair<unsigned,const std::vector<uint8_t>*>>{{boot_origin,&boot},{irq_origin,&irq},{swi_origin,&swi},{memory_origin,&memory},{copy_origin,&copy},{load_context_origin,&load_context},{bootstrap_context_origin,&bootstrap_context},{controls_origin,&controls},{control_fault_origin,&control_fault},{0xa000,&control_registry},{0x10100,&user},{0x20100,&user}})c.load(image.first,*image.second);
    c.load(0x1400,{1});c.reset();return c;
}
struct Run { density::Census census;std::vector<std::string> events;std::string header;unsigned steps=0;uint64_t nanoseconds=0; };
Run run(VirtualCPU& c,bool observe,bool console=false) {
    Run r;c.measure_traffic(observe);if(observe)r.header=c.replay_header();bool injected=false;
    auto start=std::chrono::steady_clock::now();
    while(c.state()==ExecutionState::Running && r.steps<2000000) {
        if(console && !injected && console_guest::word(c,0x402a)==2 && console_guest::word(c,0x42000)>=3 && c.mode()==Mode::User) {
            auto event=console_guest::input(c,{'h','e','l','l','o','\r','\n'});r.events.push_back(event);
            require(c.apply_boundary_json(event)==HostError::None,"input schedule");injected=true;
        }
        require(observe?r.census.step(c):c.step(),"stalled guest");++r.steps;
    }
    r.nanoseconds=std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now()-start).count();
    require(r.steps<2000000 && c.state()==ExecutionState::Halted && !c.terminal_diagnostic(),"guest must halt cleanly, PC="+std::to_string(c.registers().pc));return r;
}
void traffic_checks() {
    VirtualCPU c(0x20000);c.load(0,{0,1,0,0});c.load(0x100,{0,0x54,0x10,1,0,0,1});c.load(0x110,{0x55});c.reset();c.measure_traffic(true);
    require(c.step() && c.step_traffic().ram_reads==1 && c.step_traffic().fetch_reads==1,"NOP traffic");
    require(c.step() && c.step_traffic().ram_reads==5 && c.step_traffic().ram_writes==4,"CALLF traffic");
    require(c.step() && c.step_traffic().ram_reads==5 && c.step_traffic().fetch_reads==1 && !c.step_traffic().ram_writes,"RETF traffic");
    VirtualCPU m(0x20000);m.load(0,{0,1,0,0});m.load(0x100,{0x15,0x80,0,0x15,0xc0,0,0x15,0x88,0});m.load(0x1000,{0x55,0xaa});m.reset();m.registers().r[4]=0x1000;m.measure_traffic(true);
    require(m.step() && m.step_traffic().ram_reads==5 && m.step_traffic().fetch_reads==3 && m.registers().r[0]==0xaa55,"word load traffic");
    require(m.step() && m.step_traffic().ram_reads==3 && m.step_traffic().ram_writes==2,"word store traffic");
    require(m.step() && m.step_traffic().ram_reads==4 && m.step_traffic().fetch_reads==3 && m.registers().r[0]==0x55,"byte load traffic");
    for(bool store:{false,true}) {
        VirtualCPU f(0x20000);f.load(0,{0xfd,0xff,0,0});f.load(0xfffd,{0x15,uint8_t(store?0xc0:0x88),0});f.reset();f.registers().r[4]=0x1000;f.measure_traffic(true);f.step();
        require(f.step_traffic().ram_reads==(store?7u:8u) && f.step_traffic().fetch_reads==3 && !f.step_traffic().ram_writes,"fault retains sampled read but discards store; vector bytes counted");
    }
}
void emit(std::ostream& o,const std::string& name,const Run& r,const std::vector<uint64_t>& timings,bool first) {
    if(!first)o<<",\n";o<<"{\"name\":\""<<name<<"\",\"header\":"<<r.header<<",\"events\":[";
    for(size_t i=0;i<r.events.size();++i){if(i)o<<',';o<<r.events[i];}
    o<<"],\"instrumented_host_ns\":"<<r.nanoseconds<<",\"uninstrumented_host_ns\":[";
    for(size_t i=0;i<timings.size();++i){if(i)o<<',';o<<timings[i];}o<<"],\"census\":";r.census.json(o);o<<'}';
}
}
int main(int argc,char** argv) {
    try {
        require(argc==2,"usage: density_census report.json");traffic_checks();std::ofstream out(argv[1]);require(bool(out),"report path");
        out<<"{\"artifact_manifest_sha256\":\""<<density_images::manifest_sha256<<"\",\"programs\":[\n";bool first=true;
        for(auto& p:density_images::programs) {
            auto c=probe(p);auto observed=run(c,true);
            for(auto& [address,bytes]:p.checks)for(unsigned i=0;i<bytes.size();++i)require(c.peek(address+i)==bytes[i],p.name+": output byte "+std::to_string(address+i)+" expected "+std::to_string(bytes[i])+" got "+std::to_string(c.peek(address+i)));
            require(c.registers().sp==0xfffe,p.name+": balanced stack");
            if(p.name.rfind("descriptor_cycle_",0)==0) {
                require(c.registers().r[0]==0 && c.registers().r[1]==1 && c.registers().r[4]==0x6000 && c.csr()==0,p.name+": descriptor status/flags/caller pointer ABI");
                for(unsigned n=5;n<8;++n)require(!c.registers().r[n],p.name+": preserved B1 callee saves");
            }
            if(p.name.rfind("mul_",0)==0 || p.name.rfind("udiv_",0)==0 || p.name.rfind("sdiv_",0)==0 || p.name.rfind("parse_",0)==0 || p.name.rfind("format_",0)==0 || p.name.rfind("scale10_",0)==0) {
                for(unsigned n=4;n<8;++n)require(c.registers().r[n]==0,p.name+": arithmetic helper preserves B1");
                require(!(c.csr()&0x60),p.name+": preserves I/E");
            }
            std::vector<uint64_t> timings;
            for(unsigned sample=0;sample<5;++sample){auto d=probe(p);auto fast=run(d,false);require(fast.steps==observed.steps && same_end(c,d),p.name+": observer cannot change execution state");
                bool same=true;for(unsigned a=0;a<0x80000;++a)same&=c.peek(a)==d.peek(a);require(same,p.name+": observed/unobserved RAM mismatch");timings.push_back(fast.nanoseconds);}
            emit(out,p.name,observed,timings,first);first=false;
        }
        for(bool console:{false,true}) {
            auto c=console?console_guest::machine():minimal();auto observed=run(c,true,console);auto word=[&](unsigned a){return console_guest::word(c,a);};
            if(console)require(console_guest::finished(c) && c.protected_state()->console_tx==std::vector<uint8_t>({'H','E','L','L','O','\n'}) && word(0x300c)==1,"console outcome");
            else require(word(0x3006)>=4 && word(0x3008)==2 && word(0x3002)==1 && word(0x3004)==1,"kernel switches/nested recovery");
            std::vector<uint64_t> timings;for(unsigned i=0;i<5;++i){auto d=console?console_guest::machine():minimal();auto fast=run(d,false,console);require(fast.steps==observed.steps && fast.events==observed.events && same_end(c,d),"demo instrumentation state/schedule");
                bool same=true;for(unsigned a=0;a<0x80000;++a)same&=c.peek(a)==d.peek(a);require(same,"demo observed/unobserved RAM");timings.push_back(fast.nanoseconds);}
            emit(out,console?"console_demo":"kernel_demo",observed,timings,first);first=false;
        }
        out<<"\n]}\n";require(bool(out),"report write");std::cout<<density_images::programs.size()<<" density probes and 2 demos passed; exact traffic and profiling equivalence checked\n";
    } catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
}
