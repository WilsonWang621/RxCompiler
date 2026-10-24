#include "statement_collector.hpp"
#include "semantic_checker.hpp"

TypeId StatementCollector::collectFunctionBlock(const rx::ast::BlockExpr &body, std::optional<TypeId> expectedType){
    // 为普通嵌套块建立 ScopeGuard，检查块内容并返回尾表达式或 unit 的类型。
    ScopeGuard blockScope(checker_.scope_);
    return collectBlockContents(body, expectedType);
}

TypeId StatementCollector::collectBlockContents(const rx::ast::BlockExpr &body, std::optional<TypeId> expectedType){
    // 在已有作用域中按顺序检查语句，再按预期类型检查尾表达式并确定块类型。
    const TypeId errorType = checker_.model_.types.builtin(TypeKind::Error);
    const TypeId unitType = checker_.model_.types.builtin(TypeKind::Unit);
    const TypeId neverType = checker_.model_.types.builtin(TypeKind::Never);

    if(expectedType.has_value()){
        checker_.model_.types.get(*expectedType);
        if(*expectedType == errorType) return errorType;
    }

    bool valid = true;
    for(const auto &stmt : body.statements()){
        if(collectStmt(*stmt) == errorType) valid = false;
    }

    TypeId result;
    if(body.tail() != nullptr){
        // 不可达的尾表达式也决定块类型，并接受预期类型检查。
        result = checker_.expressionCollector_.collectExpr(*body.tail(), expectedType);
    }else{
        result = diverges(body, checker_.model_) ? neverType : unitType;
    }

    if(result != errorType && expectedType.has_value() && !checker_.typesChecker_.canCoerce(result, *expectedType)){
        checker_.diagnose(body, "block type does not match expected type");
        result = errorType;
    }
    if(!valid) result = errorType;

    checker_.model_.expressionTypes.insert_or_assign(body.id(), result);
    checker_.model_.expressionInfo.insert_or_assign(body.id(), ExprInfo{false, false});
    return result;
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
