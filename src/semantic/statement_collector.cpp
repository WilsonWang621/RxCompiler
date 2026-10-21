#include "statement_collector.hpp"
#include "semantic_checker.hpp"

TypeId StatementCollector::collectFunctionBlock(const rx::ast::BlockExpr &body, std::optional<TypeId> expectedType){
    // 为普通嵌套块建立 ScopeGuard，检查块内容并返回尾表达式或 unit 的类型。
}

TypeId StatementCollector::collectBlockContents(const rx::ast::BlockExpr &body, std::optional<TypeId> expectedType){
    // 在已有作用域中按顺序检查语句，再按预期类型检查尾表达式并确定块类型。
}

TypeId StatementCollector::collectStmt(const rx::ast::Stmt &stmt){
    // 分派空语句、let 和表达式语句，并保证带分号语句的结果类型为 unit。
}

void StatementCollector::collectLetStmt(const rx::ast::LetStmt &stmt){
    // 先检查初始化式，再确定或验证绑定类型，最后创建局部符号并加入当前作用域。
}

TypeId StatementCollector::collectExprStmt(const rx::ast::ExprStmt &stmt){
    // 检查内部表达式，并依据是否带分号返回表达式类型或 unit。
}
