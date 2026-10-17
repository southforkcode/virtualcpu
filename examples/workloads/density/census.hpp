#pragma once
// Read-only host census. Per-site key is mode/map/CS/PC before the boundary.
#include "cpu.hpp"
#include <algorithm>
#include <chrono>
#include <map>
#include <ostream>
#include <sstream>
namespace density {
struct Counts {
    uint64_t attempts=0,retired=0,reads=0,writes=0,fetch=0,samples=0,commits=0;
    uint64_t backing=0,descriptors=0;
    void json(std::ostream& o) const { o<<"{\"attempts\":"<<attempts<<",\"retired\":"<<retired<<",\"ram_read_bytes\":"<<reads<<",\"ram_write_bytes\":"<<writes<<",\"fetch_bytes\":"<<fetch<<",\"mmio_sample_bytes\":"<<samples<<",\"device_commits\":"<<commits<<",\"backing_probes\":"<<backing<<",\"descriptor_lookups\":"<<descriptors<<'}'; }
};
struct Census {
    std::map<std::string,Counts> sites;
    std::map<unsigned,uint64_t> entries;
    Counts total;
    unsigned max_depth=0,user_stack=0,kernel_stack=0;
    uint64_t max_boundary_ram_bytes=0;
    std::string max_boundary_site;
    uint64_t max_backing_probes=0,max_step_ns=0;
    std::string max_backing_site,max_step_site;
    void stack(const vcpu::VirtualCPU& c) {
        const auto& r=c.registers();
        if(r.sp<=r.st) (c.mode()==vcpu::Mode::User?user_stack:kernel_stack)=std::max(c.mode()==vcpu::Mode::User?user_stack:kernel_stack,unsigned(r.st-r.sp));
        max_depth=std::max(max_depth,unsigned(c.protected_state()->frames.size()));
    }
    bool step(vcpu::VirtualCPU& c) {
        stack(c);auto& p=*c.protected_state();
        std::ostringstream key;key<<unsigned(c.mode())<<':'<<(c.mode()==vcpu::Mode::Kernel?0:p.controls[0])<<':'<<c.registers().cs<<':'<<c.registers().pc;
        auto time=p.time,commits=p.device_commits;auto depth=p.frames.size();
        auto start=std::chrono::steady_clock::now();bool advanced=c.step();
        auto ns=uint64_t(std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now()-start).count());
        if(!advanced)return false;
        auto& t=c.step_traffic();auto& row=sites[key.str()];
        for(auto* n:{&row,&total}) {++n->attempts;n->retired+=p.time!=time;n->reads+=t.ram_reads;n->writes+=t.ram_writes;n->fetch+=t.fetch_reads;n->samples+=t.mmio_samples;n->commits+=p.device_commits-commits;n->backing+=t.backing_probes;n->descriptors+=t.descriptor_lookups;}
        if(t.ram_reads+t.ram_writes>max_boundary_ram_bytes){max_boundary_ram_bytes=t.ram_reads+t.ram_writes;max_boundary_site=key.str();}
        if(t.backing_probes>max_backing_probes){max_backing_probes=t.backing_probes;max_backing_site=key.str();}
        if(ns>max_step_ns){max_step_ns=ns;max_step_site=key.str();}
        if(p.frames.size()>depth)++entries[p.frames.back().words[17]];
        stack(c);return true;
    }
    void json(std::ostream& o) const {
        o<<"{\"totals\":";total.json(o);o<<",\"maximum_exception_depth\":"<<max_depth<<",\"maximum_user_stack_bytes\":"<<user_stack<<",\"maximum_kernel_stack_bytes\":"<<kernel_stack<<",\"maximum_observed_boundary_ram_bytes\":"<<max_boundary_ram_bytes<<",\"maximum_ram_boundary_site\":\""<<max_boundary_site<<"\",\"maximum_boundary_backing_probes\":"<<max_backing_probes<<",\"maximum_backing_boundary_site\":\""<<max_backing_site<<"\",\"maximum_instrumented_step_ns\":"<<max_step_ns<<",\"maximum_instrumented_step_site\":\""<<max_step_site<<"\",\"exception_entries\":{";
        bool first=true;for(auto [cause,count]:entries){if(!first)o<<',';first=false;o<<'"'<<cause<<"\":"<<count;}o<<"},\"sites\":{";first=true;
        for(auto& [key,row]:sites){if(!first)o<<',';first=false;o<<'"'<<key<<"\":";row.json(o);}o<<"}}";
    }
};
}
