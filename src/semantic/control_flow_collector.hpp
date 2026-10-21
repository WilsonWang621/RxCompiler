#pragma once

#include "ast.hpp"
#include "type.hpp"

#include <optional>

class SemanticChecker;

// 分支、循环和跳转表达式。
class ControlFlowCollector{
private:
    SemanticChecker &checker_;

public:
    explicit ControlFlowCollector(SemanticChecker &checker) : checker_(checker){}

    // return 使用当前函数结果类型；break / continue 使用最近一层循环上下文。
    TypeId collectIf(const rx::ast::IfExpr &ifstmt, std::optional<TypeId> expectedType = std::nullopt);
    TypeId collectLoop(const rx::ast::LoopExpr &loop, std::optional<TypeId> expectedType = std::nullopt);
    TypeId collectWhile(const rx::ast::WhileExpr &loop);
    TypeId collectReturn(const rx::ast::ReturnExpr &returnstmt);
    TypeId collectBreak(const rx::ast::BreakExpr &breakstmt);
    TypeId collectContinue(const rx::ast::ContinueExpr &continuestmt);
};
