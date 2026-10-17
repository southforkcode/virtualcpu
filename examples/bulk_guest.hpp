#pragma once
#include "cpu.hpp"
#include "bulk_images.hpp"

namespace bulk_guest {
inline vcpu::VirtualCPU machine(vcpu::MachineProfile profile=vcpu::MachineProfile::MinimalV1) {
    using namespace bulk_images;
    vcpu::VirtualCPU c(0x80000,profile);
    for(unsigned n=0;n<vcpu::exception_vector_count;++n) {
        unsigned target=n==0?boot_origin:n==6?context_origin:n==4?recover_origin:0x1400;
        c.load(4*n,{uint8_t(target),uint8_t(target>>8),0,0});
    }
    c.load(boot_origin,boot);c.load(context_origin,context);
    c.load(recover_origin,recover);c.load(load_origin,load);c.load(0x1400,{1});
    // Loader data only. Controls, records, frame edits and recovery run in guest code.
    for(unsigned a:{0x1fffu,0x2020u})c.load(a,std::vector<uint8_t>(20,0xa5));
    for(unsigned a:{0x4000u,0x4100u})c.load(a,std::vector<uint8_t>(42,0xa5));
    c.load(0x21ff,{0xa5,0,0,0,0,0xa5});c.reset();return c;
}
inline unsigned word(const vcpu::VirtualCPU& c,unsigned a) { return c.peek(a)|(unsigned(c.peek(a+1))<<8); }
inline bool finished(const vcpu::VirtualCPU& c) {
    const auto& p=*c.protected_state();
    if(c.state()!=vcpu::ExecutionState::Halted || c.registers().pc!=bulk_images::boot_origin+bulk_images::boot.size() ||
       c.context() || c.terminal_diagnostic() || c.mode()!=vcpu::Mode::Kernel ||
       !p.frames.empty() || p.published[7] || p.descriptors.at(0x703)!=std::array<uint16_t,7>{3,0x19,0,0,1,0,0} ||
       word(c,0x2200)!=0xbeef || word(c,0x2202)!=1 || c.registers().sp!=0xfffe) return false;
    for(unsigned i=0;i<18;++i)if(c.peek(0x2000+i)!=c.peek(0x2021+i))return false;
    for(unsigned a:{0x1fffu,0x2012u,0x2020u,0x2033u,0x4000u,0x4029u,0x4100u,0x4129u,0x21ffu,0x2204u})
        if(c.peek(a)!=0xa5)return false;
    return true;
}
}
