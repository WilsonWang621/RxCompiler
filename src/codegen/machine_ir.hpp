#pragma once

#include <memory>
#include <string>
#include <vector>

struct MachineInstruction{
    // 后续增加 RV32IM 操作码、寄存器、立即数、符号和栈对象等操作数。
};

struct MachineBasicBlock{
    std::string name;
    std::vector<MachineInstruction> instructions;
};

struct MachineFunction{
    std::string name;
    std::vector<std::unique_ptr<MachineBasicBlock>> blocks;
    // 后续增加参数位置、调用信息和栈对象；基本块名称在汇编输出时加函数前缀。
};

struct MachineModule{
    std::vector<std::unique_ptr<MachineFunction>> functions;
};
