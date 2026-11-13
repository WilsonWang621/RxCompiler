#pragma once

#include "machine_ir.hpp"

#include <stdexcept>

class RegisterAllocator{ // 分配物理寄存器或栈位置
public:
    void allocate(MachineFunction &) const{
        throw std::runtime_error("RISC-V register allocation is not implemented yet");
    }
};
