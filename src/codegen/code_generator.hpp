#pragma once

#include "asm_printer.hpp"
#include "calling_convention.hpp"
#include "frame_layout.hpp"
#include "instruction_selector.hpp"
#include "phi_elimination.hpp"
#include "register_allocator.hpp"

#include <sstream>
#include <string>

class CodeGenerator{ // 消费 IRModule；各阶段逐步实现，未实现时抛出异常。
private:
    InstructionSelector instructionSelector_;
    CallingConvention callingConvention_;
    PhiElimination phiElimination_;
    RegisterAllocator registerAllocator_;
    FrameLayout frameLayout_;
    AsmPrinter asmPrinter_;

public:
    std::string generate(const IRModule &module){
        auto machineModule = instructionSelector_.select(module);
        for(auto &function : machineModule.functions){
            callingConvention_.lower(*function);
            phiElimination_.run(*function);
            registerAllocator_.allocate(*function);
            frameLayout_.apply(*function);
        }
        std::ostringstream out;
        asmPrinter_.print(machineModule, out);
        return out.str();
    }
};
