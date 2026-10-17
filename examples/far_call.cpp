// Run from the repository root after assembling the two working-mode images.
#include "cpu.hpp"
#include <fstream>
#include <iterator>
#include <vector>

std::vector<uint8_t> image(const char* path) {
    std::ifstream file(path, std::ios::binary);
    return {std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};
}
int main() {
    vcpu::VirtualCPU cpu;
    cpu.load(0, {0, 1, 0, 0});
    cpu.load(0x100, image("build/far_call.bin"));
    cpu.load(0x10200, image("build/far_callee.bin"));
    cpu.reset();
    for (unsigned i=0; i<16 && cpu.state()==vcpu::ExecutionState::Running; ++i)
        cpu.step();
    return cpu.state()==vcpu::ExecutionState::Halted && !cpu.context() &&
           cpu.registers().r[0]==6 && cpu.registers().cs==0 &&
           cpu.registers().pc==0x10e && cpu.registers().sp==0xfffe ? 0 : 1;
}
