#include "module.hpp"

#include <algorithm>
#include <stdexcept>

namespace{

const char *typeName(IRType type){
    switch(type){
        case IRType::Void: return "void";
        case IRType::I32: return "i32";
        case IRType::Ptr: return "ptr";
        case IRType::I1: return "i1";
    }
    throw std::logic_error("unknown IR type");
}

const char *predicateName(IRComparePredicate predicate){
    switch(predicate){
        case IRComparePredicate::Eq: return "eq";
        case IRComparePredicate::Ne: return "ne";
        case IRComparePredicate::Sgt: return "sgt";
        case IRComparePredicate::Sge: return "sge";
        case IRComparePredicate::Slt: return "slt";
        case IRComparePredicate::Sle: return "sle";
        case IRComparePredicate::Ugt: return "ugt";
        case IRComparePredicate::Uge: return "uge";
        case IRComparePredicate::Ult: return "ult";
        case IRComparePredicate::Ule: return "ule";
    }
    throw std::logic_error("unknown IR comparison predicate");
}

const char *alignment(IRType type){
    if(type == IRType::I1) return "1";
    if(type == IRType::I32 || type == IRType::Ptr) return "4";
    throw std::logic_error("void has no storage alignment");
}

void printName(std::ostream &out, const std::string &name, char prefix = '\0'){
    if(name.empty()) throw std::logic_error("IR name cannot be empty");
    if(prefix) out << prefix;
    const auto initial = [](unsigned char ch){
        return (ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z') || ch == '$' || ch == '.' || ch == '_';
    };
    if(initial(name.front()) && std::all_of(name.begin(), name.end(), [&](unsigned char ch){
        return initial(ch) || (ch >= '0' && ch <= '9') || ch == '-';
    })){
        out << name;
        return;
    }
    const char *hex = "0123456789ABCDEF";
    out << '"';
    for(unsigned char ch : name){
        if(ch == '"' || ch == '\\' || ch < 32 || ch > 126) out << '\\' << hex[ch >> 4] << hex[ch & 15];
        else out << static_cast<char>(ch);
    }
    out << '"';
}

}

void IRModule::dump(std::ostream &out, int indent) const{
    if(indent < 0) throw std::invalid_argument("IR indentation cannot be negative");
    const std::string pad(indent, ' ');
    const auto printValue = [&out](const IRValue &value){
        if(!value.name.empty()) printName(out, value.name, '%');
        else if(value.type == IRType::Ptr){
            if(value.bits == 0) out << "null";
            else out << "inttoptr (i32 " << std::to_string(value.bits) << " to ptr)";
        }else{
            if(value.type == IRType::Void || (value.type == IRType::I1 && value.bits > 1)){
                throw std::logic_error("invalid IR constant value");
            }
            out << std::to_string(value.bits);
        }
    };
    const auto printTypedValue = [&](const IRValue &value){
        out << typeName(value.type) << ' ';
        printValue(value);
    };
    out << "target datalayout = \"e-m:e-p:32:32-i64:64-n32-S128\"\n"
        << "target triple = \"riscv32-unknown-none-elf\"\n\n";
    for(const auto &structure : structs_){
        out << pad;
        printName(out, structure->name, '%');
        out << " = type { ";
        for(std::size_t index = 0; index < structure->fields.size(); ++index){
            if(index) out << ", ";
            out << typeName(structure->fields[index]);
        }
        out << " }\n";
    }
    if(!structs_.empty()) out << '\n';
    for(const auto &function : functions_){
        if(function->isDeclaration && (function->isInternal || !function->blocks.empty())){
            throw std::logic_error("IR declaration must be external and have no body");
        }
        if(!function->isDeclaration && function->blocks.empty()) throw std::logic_error("IR definition has no blocks");
        out << pad << (function->isDeclaration ? "declare " : "define ") << (function->isInternal ? "internal " : "")
            << typeName(function->returnType) << ' ';
        printName(out, function->name, '@');
        out << '(';
        for(std::size_t index = 0; index < function->parameters.size(); ++index){
            if(index) out << ", ";
            const auto &parameter = function->parameters[index];
            out << typeName(parameter.type) << ' ';
            printName(out, parameter.name, '%');
        }
        if(function->isDeclaration){
            out << ")\n\n";
            continue;
        }
        out << ") {\n";
        for(const auto &block : function->blocks){
            if(block->instructions.empty() || block->instructions.back().opcode != IROpcode::Ret){
                throw std::logic_error("IR block has no terminator");
            }
            out << pad;
            printName(out, block->name);
            out << ":\n";
            for(const auto &instruction : block->instructions){
                out << pad << "  ";
                if(instruction.result){
                    printName(out, instruction.result->name, '%');
                    out << " = ";
                }
                switch(instruction.opcode){
                    case IROpcode::Alloca:
                        out << "alloca " << typeName(instruction.allocatedType) << ", align " << alignment(instruction.allocatedType);
                        break;
                    case IROpcode::Load:
                        out << "load " << typeName(instruction.result.value().type) << ", ";
                        printTypedValue(instruction.operands.at(0));
                        out << ", align " << alignment(instruction.result.value().type);
                        break;
                    case IROpcode::Store:
                        out << "store ";
                        printTypedValue(instruction.operands.at(0));
                        out << ", ";
                        printTypedValue(instruction.operands.at(1));
                        out << ", align " << alignment(instruction.operands.at(0).type);
                        break;
                    case IROpcode::Add: case IROpcode::Sub:
                        out << (instruction.opcode == IROpcode::Add ? "add " : "sub ");
                        printTypedValue(instruction.operands.at(0));
                        out << ", ";
                        printValue(instruction.operands.at(1));
                        break;
                    case IROpcode::ICmp:
                        out << "icmp " << predicateName(instruction.predicate) << ' ';
                        printTypedValue(instruction.operands.at(0));
                        out << ", ";
                        printValue(instruction.operands.at(1));
                        break;
                    case IROpcode::GetElementPtr:
                        if(!instruction.structure) throw std::logic_error("IR field address has no struct type");
                        instruction.structure->fields.at(instruction.fieldIndex);
                        out << "getelementptr ";
                        printName(out, instruction.structure->name, '%');
                        out << ", ";
                        printTypedValue(instruction.operands.at(0));
                        out << ", i32 0, i32 " << std::to_string(instruction.fieldIndex);
                        break;
                    case IROpcode::Call:
                        out << "call " << typeName(instruction.result ? instruction.result->type : IRType::Void) << ' ';
                        printName(out, instruction.callee, '@');
                        out << '(';
                        for(std::size_t index = 0; index < instruction.operands.size(); ++index){
                            if(index) out << ", ";
                            printTypedValue(instruction.operands[index]);
                        }
                        out << ')';
                        break;
                    case IROpcode::Ret:
                        if(instruction.operands.empty()) out << "ret void";
                        else{
                            out << "ret ";
                            printTypedValue(instruction.operands.front());
                        }
                        break;
                    default:
                        throw std::logic_error("unknown IR opcode");
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
