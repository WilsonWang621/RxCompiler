#pragma once

#include "ast.hpp"
#include "type.hpp"

#include <optional>

class SemanticChecker;

// 分支、循环和跳转表达式。
class ControlFlowCollector{
private:
    SemanticChecker &checker_;

    enum class LoopKind{ Loop, While, Condition };

    struct LoopContext{  // 保存当前这一层循环的信息
        LoopKind kind;
        std::optional<TypeId> expectedType;
        std::vector<TypeId> breakTypes;
        bool valid = true;
    };

    LoopContext *currentLoop_ = nullptr;

    struct LoopGuard{
        LoopContext *&current;
        LoopContext *previous;

        LoopGuard(LoopContext *&current, LoopContext &context): current(current), previous(current){
            current = &context;
        }

        ~LoopGuard(){ current = previous; }

        LoopGuard(const LoopGuard &) = delete;
        LoopGuard &operator=(const LoopGuard &) = delete;
    };

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
