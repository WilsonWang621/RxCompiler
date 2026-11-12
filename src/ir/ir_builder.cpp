#include "ir_builder.hpp"

#include <algorithm>
#include <stdexcept>
#include <utility>

void IRBuilder::reset(IRModule *module){
    module_ = module;
    function_ = nullptr;
    block_ = nullptr;
    nextValueId_ = 0;
}

IRStruct &IRBuilder::declareStruct(std::string name, std::vector<IRType> fields){
    if(!module_) throw std::logic_error("IRBuilder has no module");
    for(IRType type : fields){
        if(type == IRType::Void) throw std::invalid_argument("IR struct field cannot have void type");
    }
    auto structure = std::make_unique<IRStruct>();
    structure->name = std::move(name);
    structure->fields = std::move(fields);
    module_->structs_.push_back(std::move(structure));
    return *module_->structs_.back();
}

IRFunction &IRBuilder::declareFunction(std::string name, IRType returnType, std::vector<IRParameter> parameters, bool isInternal){
    if(!module_) throw std::logic_error("IRBuilder has no module");
    for(const auto &parameter : parameters){
        if(parameter.type == IRType::Void) throw std::invalid_argument("IR void parameters are not supported");
    }
    auto function = std::make_unique<IRFunction>();
    function->name = std::move(name);
    function->returnType = returnType;
    function->parameters = std::move(parameters);
    function->isInternal = isInternal;
    function->isDeclaration = true;
    module_->functions_.push_back(std::move(function));
    return *module_->functions_.back();
}

bool IRBuilder::ownsFunction(const IRFunction &function) const{ 
    return module_ && std::any_of(module_->functions_.begin(), module_->functions_.end(), [&](const auto &candidate){ return candidate.get() == &function;});
}

bool IRBuilder::ownsBlock(const IRBasicBlock &block) const{
    return function_ && std::any_of(function_->blocks.begin(), function_->blocks.end(), [&](const auto &candidate){ return candidate.get() == &block;});
}

void IRBuilder::beginFunction(IRFunction &function){
    if(!module_) throw std::logic_error("IRBuilder has no module");
    if(!ownsFunction(function)) throw std::invalid_argument("function does not belong to the current module");
    if(!function.isDeclaration || !function.blocks.empty()) throw std::logic_error("IR function body is already started");
    function.isDeclaration = false;
    function_ = &function;
    block_ = nullptr;
    nextValueId_ = 0;
}

IRBasicBlock &IRBuilder::createBlock(IRFunction &function, std::string name){
    if(!module_) throw std::logic_error("IRBuilder has no module");
    if(!ownsFunction(function)) throw std::invalid_argument("function does not belong to the current module");
    if(function.isDeclaration) throw std::logic_error("IR function body has not started");
    if(name.empty()) throw std::invalid_argument("IR block name cannot be empty");
    for(const auto &block : function.blocks){
        if(block->name == name) throw std::invalid_argument("duplicate IR block name");
    }
    auto block = std::make_unique<IRBasicBlock>();
    block->name = std::move(name);
    function.blocks.push_back(std::move(block));
    return *function.blocks.back();
}

IRBasicBlock &IRBuilder::createBlock(std::string name){
    if(!function_) throw std::logic_error("IRBuilder has no function");
    return createBlock(*function_, std::move(name));
}

void IRBuilder::setInsertPoint(IRBasicBlock &block){
    if(!function_) throw std::logic_error("IRBuilder has no function");
    if(!ownsBlock(block)) throw std::invalid_argument("block does not belong to the current function");
    block_ = &block;
}

bool IRBuilder::isTerminated() const{
    if(!block_) throw std::logic_error("IRBuilder has no insertion block");
    if(block_->instructions.empty()) return false;
    const IROpcode opcode = block_->instructions.back().opcode;
    return opcode == IROpcode::Ret || opcode == IROpcode::Br;
}

void IRBuilder::append(IRInstruction instruction){
    if(isTerminated()) throw std::logic_error("cannot append an instruction after a terminator");
    block_->instructions.push_back(std::move(instruction));
}

IRValue IRBuilder::createAlloca(IRType type){
    if(type == IRType::Void) throw std::invalid_argument("alloca requires a value type");
    IRValue address{IRType::Ptr, "v" + std::to_string(nextValueId_), 0};
    IRInstruction instruction{IROpcode::Alloca, address, {}, {}};
    instruction.allocatedType = type;
    append(std::move(instruction));
    ++nextValueId_;
    return address;
}

void IRBuilder::createStore(IRValue value, IRValue address){
    if(value.type == IRType::Void || address.type != IRType::Ptr || address.name.empty()){
        throw std::invalid_argument("store requires a value and a local pointer");
    }
    append({IROpcode::Store, {}, {std::move(value), std::move(address)}, {}});
}

IRValue IRBuilder::createLoad(IRType type, IRValue address){
    if(type == IRType::Void || address.type != IRType::Ptr || address.name.empty()){
        throw std::invalid_argument("load requires a value type and a local pointer");
    }
    IRValue result{type, "v" + std::to_string(nextValueId_), 0};
    append({IROpcode::Load, result, {std::move(address)}, {}});
    ++nextValueId_;
    return result;
}

std::optional<IRValue> IRBuilder::createCall(const IRFunction &callee, std::vector<IRValue> arguments){
    if(!ownsFunction(callee)) throw std::invalid_argument("callee does not belong to the current module");
    if(arguments.size() != callee.parameters.size()) throw std::invalid_argument("call argument count does not match");
    for(std::size_t index = 0; index < arguments.size(); ++index){
        if(arguments[index].type != callee.parameters[index].type) throw std::invalid_argument("call argument type does not match");
    }
    std::optional<IRValue> result;
    if(callee.returnType != IRType::Void){
        result = IRValue{callee.returnType, "v" + std::to_string(nextValueId_), 0};
    }
    append({IROpcode::Call, result, std::move(arguments), callee.name});
    if(result.has_value()) ++nextValueId_;
    return result;
}

void IRBuilder::createReturn(std::optional<IRValue> value){
    if(!function_) throw std::logic_error("IRBuilder has no function");
    if(value ? value->type == IRType::Void || value->type != function_->returnType : function_->returnType != IRType::Void){
        throw std::invalid_argument("return value does not match the function type");
    }
    IRInstruction instruction{IROpcode::Ret, {}, {}, {}};
    if(value) instruction.operands.push_back(*value);
    append(std::move(instruction));
}

IRValue IRBuilder::createBinary(IROpcode opcode, IRValue left, IRValue right){
    switch(opcode){
        case IROpcode::Add:
        case IROpcode::Sub:
        case IROpcode::Mul:
        case IROpcode::SDiv:
        case IROpcode::UDiv:
        case IROpcode::SRem:
        case IROpcode::URem:
        case IROpcode::Shl:
        case IROpcode::LShr:
        case IROpcode::AShr:
        case IROpcode::And:
        case IROpcode::Or:
        case IROpcode::Xor:
            break;
        default: throw std::invalid_argument("invalid binary opcode");
    }

    const bool bitwise = opcode == IROpcode::And || opcode == IROpcode::Or || opcode == IROpcode::Xor;
    if(left.type != right.type || (left.type != IRType::I32 && !(bitwise && left.type == IRType::I1))){
        throw std::invalid_argument("binary operands must have matching supported integer types");
    }

    IRValue result{left.type, "v" + std::to_string(nextValueId_++), 0};
    append({opcode, result, {std::move(left), std::move(right)}, {}});
    return result;
}

IRValue IRBuilder::createCompare(IRComparePredicate predicate, IRValue left, IRValue right){
    if(left.type != right.type || (left.type != IRType::I32 && left.type != IRType::I1)){
        throw std::invalid_argument("comparison requires matching integer operands");
    }
    IRValue result{IRType::I1, "v" + std::to_string(nextValueId_++), 0};
    IRInstruction instruction{IROpcode::ICmp, result, {std::move(left), std::move(right)}, {}};
    instruction.predicate = predicate;
    append(instruction);
    return result;
}

IRValue IRBuilder::createZExt(IRValue value){
    if(value.type != IRType::I1){
        throw std::invalid_argument("IR zero extension requires an i1 value");
    }

    IRValue result{IRType::I32, "v" + std::to_string(nextValueId_++), 0};
    append({IROpcode::ZExt, result, {std::move(value)}, {}});
    return result;
}

void IRBuilder::createIf(IRValue condition, IRBasicBlock &ifBlock, IRBasicBlock &elseBlock){
    if(condition.type != IRType::I1) throw std::invalid_argument("IR if condition requires an i1 value");
    if(!ownsBlock(ifBlock) || !ownsBlock(elseBlock)) throw std::invalid_argument("branch target does not belong to the current function");
    IRInstruction instruction{IROpcode::Br, {}, {std::move(condition)}, {}};
    instruction.destination = {&ifBlock, &elseBlock};
    append(std::move(instruction));
}

void IRBuilder::createBranch(IRBasicBlock &destination){
    if(!ownsBlock(destination)) throw std::invalid_argument("branch target does not belong to the current function");
    IRInstruction instruction{IROpcode::Br, {}, {}, {}};
    instruction.destination = {&destination};
    append(std::move(instruction));
}

