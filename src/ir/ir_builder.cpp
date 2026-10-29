#include "ir_builder.hpp"

#include <stdexcept>
#include <utility>

void IRBuilder::reset(IRModule *module){
    module_ = module;
    function_ = nullptr;
    block_ = nullptr;
    nextValueId_ = 0;
}

IRFunction &IRBuilder::createFunction(std::string name, IRType returnType, bool isInternal){
    if(!module_) throw std::logic_error("IRBuilder has no module");
    if(returnType == IRType::Ptr) throw std::invalid_argument("IR sample supports void or i32 returns");
    auto function = std::make_unique<IRFunction>();
    function->name = std::move(name);
    function->returnType = returnType;
    function->isInternal = isInternal;
    module_->functions_.push_back(std::move(function));
    function_ = module_->functions_.back().get();
    block_ = nullptr;
    nextValueId_ = 0;
    return *function_;
}

void IRBuilder::createBlock(std::string name){
    if(!function_) throw std::logic_error("IRBuilder has no function");
    auto block = std::make_unique<IRBasicBlock>();
    block->name = std::move(name);
    function_->blocks.push_back(std::move(block));
    block_ = function_->blocks.back().get();
}

void IRBuilder::append(IRInstruction instruction){
    if(!block_) throw std::logic_error("IRBuilder has no insertion block");
    if(!block_->instructions.empty() && block_->instructions.back().opcode == IROpcode::Ret){
        throw std::logic_error("cannot append an instruction after return");
    }
    block_->instructions.push_back(std::move(instruction));
}

IRValue IRBuilder::createAlloca(){
    IRValue address{IRType::Ptr, "v" + std::to_string(nextValueId_++), 0};
    append({IROpcode::Alloca, address, {}, {}});
    return address;
}

void IRBuilder::createStore(IRValue value, IRValue address){
    if(value.type != IRType::I32 || address.type != IRType::Ptr || address.name.empty()){
        throw std::invalid_argument("store requires an i32 value and a local pointer");
    }
    append({IROpcode::Store, {}, {std::move(value), std::move(address)}, {}});
}

void IRBuilder::createCall(const IRFunction &callee){
    if(callee.returnType != IRType::Void) throw std::invalid_argument("IR sample requires a void callee");
    append({IROpcode::Call, {}, {}, callee.name});
}

void IRBuilder::createReturn(std::optional<IRValue> value){
    if(!function_ || (value ? value->type != IRType::I32 || function_->returnType != IRType::I32 : function_->returnType != IRType::Void)){
        throw std::invalid_argument("return value does not match the function type");
    }
    IRInstruction instruction{IROpcode::Ret, {}, {}, {}};
    if(value) instruction.operands.push_back(*value);
    append(std::move(instruction));
}
