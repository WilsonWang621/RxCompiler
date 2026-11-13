#pragma once

#include "../ir/module.hpp"
#include "machine_ir.hpp"

#include <stdexcept>

class InstructionSelector{ // 将现有 IR 转换成机器 IR，保留调用和 phi 等待降低的操作。
public:
    MachineModule select(const IRModule &) const{
        throw std::runtime_error("RISC-V instruction selection is not implemented yet");
    }
};
