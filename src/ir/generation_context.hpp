#pragma once

#include "ir_builder.hpp"
#include "semantic_model.hpp"
#include "type_lowering.hpp"

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
    bool terminated = false; // true 表示当前块已终结，此时 value 必须为空。
};

struct IRStructLayout{
    IRStruct *structure = nullptr; // 非拥有指针，实体由返回的 Module 拥有。
    std::unordered_map<std::string, std::size_t> fieldIndices;
};

struct IRGenerationContext{ //各生成器显式共享本次编译的状态，不拥有 AST 或语义模型。
    IRBuilder builder;
    TypeLowering typeLowering;
    const SemanticModel *model = nullptr; // 仅 reset 到 clear 之间有效。
    std::unordered_map<SymbolId, IRValue> locals; // 参数、self、局部变量 -> 栈槽。
    std::unordered_map<SymbolId, IRValue> constants; // 全局和关联标量常量。
    std::unordered_map<SymbolId, IRFunction *> functions;
    std::unordered_map<SymbolId, IRStructLayout> structs;
    SymbolId currentFunction;

    void reset(IRModule &module, const SemanticModel &model); // 清表并绑定 Builder。
    void clear(); // 解除绑定并清除非拥有指针；不销毁 Module 中的 IR。
};
