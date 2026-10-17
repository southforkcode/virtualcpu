#pragma once
#include "cpu.hpp"
#include "kernel_images.hpp"
#include <sstream>

namespace console_guest {
inline void word(vcpu::VirtualCPU& c,unsigned a,unsigned value) {
    c.load(a,{uint8_t(value),uint8_t(value>>8)});
}
inline unsigned word(const vcpu::VirtualCPU& c,unsigned a) {
    return c.peek(a)|(unsigned(c.peek(a+1))<<8);
}
inline vcpu::VirtualCPU machine(unsigned count=32,unsigned pointer=0x2000,unsigned segment=3,unsigned peer=0,unsigned hole=0,bool probe=false) {
    using namespace kernel_images;
    vcpu::VirtualCPU c(0x80000,vcpu::MachineProfile::ConsoleV1);
    for (unsigned n=0;n<vcpu::exception_vector_count;++n) {
        unsigned target=n==0?0x100:n==5?console_irq:n==6?console_swi:0x1800;
        c.load(4*n,{uint8_t(target),uint8_t(target>>8),0,0});
    }
    c.load(console_boot_origin,console_boot); c.load(console_origin,console);
    c.load(console_memory_origin,console_memory); c.load(load_context_origin,load_context); c.load(bootstrap_context_origin,bootstrap_context);
    c.load(controls_origin,controls); c.load(control_fault_origin,control_fault); c.load(0xa000,control_registry);
    c.load(0x1400,{1}); c.load(0x10100,console_user); c.load(0x20100,console_peer);
    word(c,0x32100,count); word(c,0x32102,pointer); word(c,0x32104,segment); word(c,0x42010,peer);
    if (hole) c.map(hole,1,false);
    if (probe) {c.load(console_probe_origin,console_probe);c.load(20,{0,0x1c,0,0});}
    c.reset(); return c;
}
inline std::string input(const vcpu::VirtualCPU& c,const std::vector<uint8_t>& bytes) {
    static constexpr char hex[]="0123456789abcdef";
    std::ostringstream out;
    out<<"{\"run\":\""<<c.run()<<"\",\"boundary\":\""<<c.boundary()<<"\",\"sequence\":\""<<c.next_sequence()
       <<"\",\"kind\":\"console_rx\",\"data\":\"";
    for (auto b:bytes) out<<hex[b>>4]<<hex[b&15];
    out<<"\"}"; return out.str();
}
inline bool finished(const vcpu::VirtualCPU& c) {
    return c.state()==vcpu::ExecutionState::Halted && c.registers().pc==kernel_images::console_finished+1;
}
}
