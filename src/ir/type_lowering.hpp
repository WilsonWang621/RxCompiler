#pragma once

#include "ir.hpp"
#include "semantic_model.hpp"

class TypeLowering{ //只转换已解析类型，不负责名称解析、类型检查或生成指令。
public:
    // 当前覆盖 unit -> Void、bool -> I1、整数 -> I32、引用 -> Ptr。
    // 结构体的字段声明由 DeclarationGenerator 登记到 IRStruct。
    IRType lowerType(TypeId type, const SemanticModel &model) const;
};
