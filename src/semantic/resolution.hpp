#pragma once

#include "ast.hpp"
#include "symbol.hpp"
#include "type.hpp"

#include <optional>

class SemanticChecker;

// 类型解析和路径解析。
class Resolution{ //负责把ast类型的信息转化为semantic模型可以用的信息
    SemanticChecker &checker_;

public:
    explicit Resolution(SemanticChecker &checker) : checker_(checker){}

    // 两类：类型语法TypeRef -> TypeId  值语法PathExpr -> SymbolId
    TypeId resolveType(const rx::ast::TypeRef &type, std::optional<TypeId> selfType = std::nullopt); //解析完整的类型结构
    TypeId resolveTypePath(const rx::ast::TypePathRef &type, std::optional<TypeId> selfType = std::nullopt); //确定一个类型名字到底代表什么
    std::optional<SymbolId> resolveValuePath(const rx::ast::PathExpr &path); //把值路径解析到符号
};
