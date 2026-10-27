#include "module.hpp"

void IRModule::dump(std::ostream &out, int indent) const{
    const std::string pad(indent, ' ');
    const auto printValue = [&out](const IRValue &value){
        if(value.name.empty()) out << value.bits;
        else out << '%' << value.name;
    };
    out << "target datalayout = \"e-m:e-p:32:32-i64:64-n32-S128\"\n"
        << "target triple = \"riscv32-unknown-unknown-elf\"\n\n";
    for(const auto &function : functions_){
        out << pad << "define " << (function->isInternal ? "internal " : "")
            << (function->returnType == IRType::Void ? "void" : "i32")
            << " @" << function->name << "() {\n";
        for(const auto &block : function->blocks){
            out << pad << block->name << ":\n";
            for(const auto &instruction : block->instructions){
                out << pad << "  ";
                switch(instruction.opcode){
                    case IROpcode::Alloca:
                        out << '%' << instruction.result->name << " = alloca i32, align 4";
                        break;
                    case IROpcode::Store:
                        out << "store i32 ";
                        printValue(instruction.operands.at(0));
                        out << ", ptr ";
                        printValue(instruction.operands.at(1));
                        out << ", align 4";
                        break;
                    case IROpcode::Call:
                        out << "call void @" << instruction.callee << "()";
                        break;
                    case IROpcode::Ret:
                        if(instruction.operands.empty()) out << "ret void";
                        else{
                            out << "ret i32 ";
                            printValue(instruction.operands.front());
                        }
                        break;
                }
                out << '\n';
            }
        }
        out << pad << "}\n\n";
    }
}

const std::vector<std::unique_ptr<IRFunction>>& IRModule::functions() const{
    return functions_;
}

const std::vector<std::unique_ptr<IRStruct>>& IRModule::structs() const{
    return structs_;
}