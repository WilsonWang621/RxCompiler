#pragma once

#include "ast.hpp"
#include "semantic_model.hpp"

#include <cstdint>
#include <optional>

class SemanticChecker;

// 常量求值和数组长度。
class ConstEvaluate{ //编译期的常量求值器
private:
    SemanticChecker &checker_;

public:
    explicit ConstEvaluate(SemanticChecker &checker) : checker_(checker){}

    // 常量按依赖求值并检测循环；长度求值失败用 nullopt，避免与合法长度 0 混淆；还检查类型，检查依赖
    std::optional<ConstValue> evaluateConstant(SymbolId symbol);
    std::optional<ConstValue> evaluateConstantExpr(const rx::ast::Expr &expression, TypeId expectedType);
    std::optional<std::uint32_t> evaluateArrayLength(const rx::ast::Expr &expression); //专门限制为 usize
};
