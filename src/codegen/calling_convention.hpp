#pragma once

#include "machine_ir.hpp"

#include <stdexcept>

class CallingConvention{ // 安排参数和返回值位置，记录调用破坏和保存的寄存器。
public:
    void lower(MachineFunction &) const{
        throw std::runtime_error("RISC-V calling convention lowering is not implemented yet");
    }
};
