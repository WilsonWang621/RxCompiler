#pragma once

#include "machine_ir.hpp"

#include <stdexcept>

class FrameLayout{ // 确定栈帧大小与偏移，建立序言和尾声，并处理大栈偏移。
public:
    void apply(MachineFunction &) const{
        throw std::runtime_error("RISC-V frame layout is not implemented yet");
    }
};
