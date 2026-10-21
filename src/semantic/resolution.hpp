#pragma once

#include "ast.hpp"
#include "symbol.hpp"
#include "type.hpp"

#include <optional>

class SemanticChecker;

// 类型解析和路径解析。
class Resolution{
private:
    SemanticChecker &checker_;

public:
    explicit Resolution(SemanticChecker &checker) : checker_(checker){}

    // TypeRef -> TypeId；selfType 只在当前结构体及其 impl 的类型上下文有效。
    TypeId resolveType(const rx::ast::TypeRef &type, std::optional<TypeId> selfType = std::nullopt);
    TypeId resolveTypePath(const rx::ast::TypePathRef &type, std::optional<TypeId> selfType = std::nullopt);
    std::optional<SymbolId> resolveValuePath(const rx::ast::PathExpr &path);
};
