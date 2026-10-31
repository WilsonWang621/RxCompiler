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

void IRBuilder::setInsertPoint(IRBasicBlock &block){
    if(!function_) throw std::logic_error("IRBuilder has no function");
    const auto found = std::find_if(function_->blocks.begin(), function_->blocks.end(),
        [&](const auto &candidate){ return candidate.get() == &block; });
    if(found == function_->blocks.end()) throw std::invalid_argument("block does not belong to the current function");
    block_ = &block;
}

bool IRBuilder::isTerminated() const{
    if(!block_) throw std::logic_error("IRBuilder has no insertion block");
    // 当前 IR 仅有 Ret 终结指令；加入分支指令时在这里扩展判断。
    return !block_->instructions.empty() && block_->instructions.back().opcode == IROpcode::Ret;
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
