#pragma once

#include "ast.hpp"
#include "type.hpp"

#include <optional>

class SemanticChecker;

// 块、语句和局部绑定。
class StatementCollector{
private:
    SemanticChecker &checker_;

public:
    explicit StatementCollector(SemanticChecker &checker) : checker_(checker){}

    // 块入口管理作用域；函数体已声明参数后使用 collectBlockContents，避免重复入栈。
    TypeId collectFunctionBlock(const rx::ast::BlockExpr &body, std::optional<TypeId> expectedType = std::nullopt);
    TypeId collectBlockContents(const rx::ast::BlockExpr &body, std::optional<TypeId> expectedType = std::nullopt);
    TypeId collectStmt(const rx::ast::Stmt &stmt);
    void collectLetStmt(const rx::ast::LetStmt &stmt);             // let x : i32 = 20;
    TypeId collectExprStmt(const rx::ast::ExprStmt &stmt);
};
