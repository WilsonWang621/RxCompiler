#pragma once

#include "expression_generator.hpp"

class StatementGenerator{ //生成语句和块，处理声明的存储与尾表达式。
private:
    ExpressionGenerator expressionGenerator_;
    void generateLet(const rx::ast::LetStmt &stmt, IRGenerationContext &context);

public:
    IRExprResult generateStmt(const rx::ast::Stmt &stmt, IRGenerationContext &context);
    // 语句依次执行；终结后停止；尾表达式的值交给 FunctionGenerator 决定返回。
    IRExprResult generateBlock(const rx::ast::BlockExpr &block, IRGenerationContext &context);
};
