#pragma once

#include <ast.hpp>
#include "declaration_table.hpp"
#include "call_resolution.hpp"
#include "symbol.hpp"
#include "type.hpp"

#include <unordered_map>

struct ExprInfo{
    bool mut;
    bool isPlace;
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
    // 拥有本次检查产生的类型；可以typeId 和 type互查。
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

    // CallExpr / MethodCallExpr 节点 -> 已选定目标、完整签名和接收者调整。
    // 仅在调用检查成功后登记；IR 不再根据方法名重新执行查找。
    std::unordered_map<rx::ast::NodeId, ResolvedCall> resolvedCalls;

    // 实际 coercion 位置的表达式节点 -> 转换计划，不覆盖 expressionTypes。
    // block / if / loop 传播目标时在实际结果处记录，避免包装节点重复解引用。
    std::unordered_map<rx::ast::NodeId, CoercionPlan> coercions;

    std::vector<Diagnosis> diagnostics;
};
