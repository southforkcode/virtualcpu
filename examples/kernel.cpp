// Guest-only protected boot, timer scheduling and nested user-copy recovery.
#include "cpu.hpp"
#include "kernel_images.hpp"
#include <iostream>

int main() {
    vcpu::VirtualCPU cpu(0x80000);
    for (unsigned n=0;n<8;++n) {
        unsigned target=n==0?0x100:n==5?0x1000:n==6?0x1200:0x1800;
        cpu.load(4*n,{uint8_t(target),uint8_t(target>>8),0,0});
    }
    using namespace kernel_images;
    cpu.load(boot_origin,boot); cpu.load(irq_origin,irq); cpu.load(swi_origin,swi);
    cpu.load(memory_origin,memory); cpu.load(copy_origin,copy); cpu.load(load_context_origin,load_context);
    cpu.load(controls_origin,controls); cpu.load(control_fault_origin,control_fault);
    cpu.load(0xa000,control_registry);
    cpu.load(0x1400,{1}); cpu.load(0x10100,user); cpu.load(0x20100,user); cpu.reset();
    unsigned steps=0;
    while (cpu.state()==vcpu::ExecutionState::Running && steps<20000) { cpu.step(); ++steps; }
    auto word=[&](unsigned a) { return cpu.peek(a)|(unsigned(cpu.peek(a+1))<<8); };
    bool ok=cpu.state()==vcpu::ExecutionState::Halted && cpu.registers().pc==swi_origin+swi.size() &&
            word(0x3002)==1 && word(0x3004)==1 && word(0x3006)>=4 && word(0x3008)==2 &&
            word(0x32000)>=3 && word(0x42000)>=3 && !cpu.terminal_diagnostic();
    std::cout<<(ok?"PASS":"FAIL")<<": "<<steps<<" boundaries, "<<word(0x3006)
             <<" timer switches, "<<word(0x3008)<<" recovered user-copy faults\n";
    return ok?0:1;
}
