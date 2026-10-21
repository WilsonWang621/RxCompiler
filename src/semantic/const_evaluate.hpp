#pragma once

#include "ast.hpp"
#include "semantic_model.hpp"

#include <cstdint>
#include <optional>

class SemanticChecker;

// 常量求值和数组长度。
class ConstEvaluate{
private:
    SemanticChecker &checker_;

public:
    explicit ConstEvaluate(SemanticChecker &checker) : checker_(checker){}

    // 常量按依赖求值并检测循环；长度求值失败用 nullopt，这样不与合法长度 0 混淆。
    std::optional<ConstValue> evaluateConstant(SymbolId symbol);
    std::optional<ConstValue> evaluateConstantExpr(const rx::ast::Expr &expression, TypeId expectedType);
    std::optional<std::uint32_t> evaluateArrayLength(const rx::ast::Expr &expression);
};
