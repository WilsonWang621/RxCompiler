#pragma once

#include "machine_ir.hpp"

#include <ostream>
#include <stdexcept>

class AsmPrinter{ // 输出 RV32IM 汇编、函数标签及常量数据。
public:
    void print(const MachineModule &, std::ostream &) const{
        throw std::runtime_error("RISC-V assembly printing is not implemented yet");
    }
};
