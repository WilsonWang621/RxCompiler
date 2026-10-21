#pragma once

#include "ast.hpp"
#include "type.hpp"

#include <vector>

class SemanticChecker;

// 类型转换、公共类型、trait 和结构体校验。
class TypesChecker{
private:
    SemanticChecker &checker_;

public:
    explicit TypesChecker(SemanticChecker &checker) : checker_(checker){}

    // 类型相等、隐式转换与 as 转换是不同规则；合并类型时保留所有先前结果。
    bool canCoerce(TypeId source, TypeId target) const;
    bool canCast(TypeId source, TypeId target) const;
    TypeId collectCommonType(const std::vector<TypeId> &types, const rx::ast::ASTNode &node);
    bool hasCopy(TypeId type) const;
    bool hasClone(TypeId type) const;
    bool hasPartialEq(TypeId type) const;
    bool hasEq(TypeId type) const;
    void checkStructLayout(const rx::ast::StructItem &structure);
    void checkStructDerive(const rx::ast::StructItem &structure);
};
