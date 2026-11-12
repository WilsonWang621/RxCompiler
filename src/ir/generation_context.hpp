#pragma once

#include "ir_builder.hpp"
#include "semantic_model.hpp"
#include "type_lowering.hpp"

#include <stack>
#include <cstddef>
#include <optional>
#include <string>
#include <unordered_map>

struct IRPlace{
    IRValue address;
    TypeId type; // 被存储值的语义类型，供自动解引用和字段选择使用。
};

struct IRExprResult{
    std::optional<IRValue> value; // 普通值有值；unit 或发散表达式没有值。
    bool terminated = false; // true 表示当前块已终结，此时 value 为空。
};

struct IRStructLayout{
    IRStruct *structure = nullptr; 
    std::unordered_map<std::string, std::size_t> fieldIndices; //name -> 在struct.field中的下标
};

struct IRLoop{
    IRBasicBlock *continueBlock = nullptr; //下一轮开始位置
    IRBasicBlock *breakblock = nullptr; //离开循环的位置
    IRType resultType = IRType::Void;
    std::optional<IRValue> resultAddress = std::nullopt; // break 值的存储位置；unit 无需存储。
    std::string exitName = {}; // loop 首次实际退出时才创建出口块。
};

struct IRGenerationContext{ //真正的核心，拥有builder这样的构建器和semanticModel这样的资料库
    IRBuilder builder; 
    TypeLowering typeLowering;
    const SemanticModel *model = nullptr;
    std::unordered_map<SymbolId, IRValue> locals;   // 参数、self、局部变量
    std::unordered_map<SymbolId, IRValue> constants; // 全局和关联标量常量。
    std::unordered_map<SymbolId, IRFunction *> functions;
    std::unordered_map<SymbolId, IRStructLayout> structs;
    std::stack<IRLoop> loops;
    SymbolId currentFunction;

    void reset(IRModule &module, const SemanticModel &model); //重置+重新绑定 model 和 module
    void clear(); 
};
