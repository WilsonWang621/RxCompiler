#pragma once

#include <ast.hpp>
#include "declaration_table.hpp"
#include "symbol.hpp"
#include "type.hpp"

#include <unordered_map>

struct ExprInfo{
    bool mut;
    
};

struct ConstValue{
    TypeId type;
    // 整数保存 32 位补码位模式；bool 保存 0 或 1，具体含义由 type 区分。
    std::uint32_t bits = 0;
};

struct Diagnosis{
    rx::ast::SourceSpan span;
    std::string message;
};

struct SemanticModel{
    // 拥有本次检查产生的类型；所有 TypeId 均相对于此表。
    TypeTable types;

     // 第一次扫描产生的声明表
    DeclarationTable declarations;

    // SymbolId -> 符号的永久信息
    std::unordered_map<SymbolId, SymbolInfo> symbols;

    // 参数、接收者和局部变量符号 -> 已解析的类型。
    std::unordered_map<SymbolId, TypeId> symbolTypes;

    // 声明节点 -> 为它分配的 SymbolId
    std::unordered_map<rx::ast::NodeId, SymbolId> declaredSymbols;

    // 名称使用节点 -> 最终解析到的 SymbolId
    std::unordered_map<rx::ast::NodeId, SymbolId> resolvedSymbols;

    // 表达式节点 -> 表达式类型
    std::unordered_map<rx::ast::NodeId, TypeId> expressionTypes;

    // TypeRef 节点 -> 解析后的类型
    std::unordered_map<rx::ast::NodeId, TypeId> resolvedTypes;

    // 编译期可求值表达式 -> 常量值
    std::unordered_map<rx::ast::NodeId, ConstValue> constantValues;

    // 表达式是否为 place、是否可变等属性
    std::unordered_map<rx::ast::NodeId, ExprInfo> expressionInfo;

    std::vector<Diagnosis> diagnostics;
};
