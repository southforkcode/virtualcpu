#pragma once

namespace vcpu {

#include <stdint.h>

class VirtualCPU {
public:

private:

uint16_t regs[8];
uint16_t pco; // program counter offset
uint16_t pcp; // program counter page
uint16_t csr; // cpu status register

};

}