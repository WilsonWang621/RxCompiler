#pragma once

#include "machine_ir.hpp"

#include <stdexcept>

class PhiElimination{ // 将 phi 转换为边上的并行拷贝，必要时拆分控制流边。
public:
    void run(MachineFunction &) const{
        throw std::runtime_error("RISC-V phi elimination is not implemented yet");
    }
};
