#pragma once

#include <ast.hpp>
#include "declaration_table.hpp"
#include "symbol.hpp"

#include <unordered_map>

struct TypeId   { uint32_t value; };


struct ExprInfo{
    bool mut;
    
};

struct ConstValue{

};

struct Diagnosis{
    rx::ast::SourceSpan span;
    std::string message;
};

struct SemanticModel{
     // 第一次扫描产生的声明表
    DeclarationTable declarations;

    // SymbolId -> 符号的永久信息
    std::unordered_map<SymbolId, SymbolInfo> symbols;

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
