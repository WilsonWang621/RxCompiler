#include "statement_collector.hpp"
#include "semantic_checker.hpp"

#include <algorithm>

namespace{

namespace ast = rx::ast;

// 只读取已经检查过的 AST 和语义结果，不重复执行语义检查。
template<class Visitor>
bool anyChild(const ast::Expr &expression, Visitor visit){
    if(const auto *node = dynamic_cast<const ast::UnaryExpr *>(&expression)){
        return visit(node->operand());
    }
    if(const auto *node = dynamic_cast<const ast::CastExpr *>(&expression)){
        return visit(node->value());
    }
    if(const auto *node = dynamic_cast<const ast::BinaryExpr *>(&expression)){
        return visit(node->left()) || visit(node->right());
    }
    if(const auto *node = dynamic_cast<const ast::AssignExpr *>(&expression)){
        return visit(node->target()) || visit(node->value());
    }
    if(const auto *node = dynamic_cast<const ast::CompoundAssignExpr *>(&expression)){
        return visit(node->target()) || visit(node->value());
    }
    if(const auto *node = dynamic_cast<const ast::FieldExpr *>(&expression)){
        return visit(node->base());
    }
    if(const auto *node = dynamic_cast<const ast::IndexExpr *>(&expression)){
        return visit(node->base()) || visit(node->index());
    }
    if(const auto *node = dynamic_cast<const ast::ArrayExpr *>(&expression)){
        return std::any_of(node->elements().begin(), node->elements().end(),
            [&](const auto &element){ return visit(*element); });
    }
    if(const auto *node = dynamic_cast<const ast::ArrayRepeatExpr *>(&expression)){
        return visit(node->value());
    }
    if(const auto *node = dynamic_cast<const ast::StructExpr *>(&expression)){
        return std::any_of(node->fields().begin(), node->fields().end(),
            [&](const auto &field){ return visit(field->value()); });
    }
    if(const auto *node = dynamic_cast<const ast::CallExpr *>(&expression)){
        return visit(node->callee()) ||
               std::any_of(node->arguments().begin(), node->arguments().end(),
                   [&](const auto &argument){ return visit(*argument); });
    }
    if(const auto *node = dynamic_cast<const ast::MethodCallExpr *>(&expression)){
        return visit(node->receiver()) ||
               std::any_of(node->arguments().begin(), node->arguments().end(),
                   [&](const auto &argument){ return visit(*argument); });
    }
    if(const auto *node = dynamic_cast<const ast::BlockExpr *>(&expression)){
        for(const auto &stmt : node->statements()){
            if(const auto *binding = dynamic_cast<const ast::LetStmt *>(stmt.get())){
                if(visit(binding->initializer())) return true;
            }else if(const auto *statement = dynamic_cast<const ast::ExprStmt *>(stmt.get())){
                if(visit(statement->expression())) return true;
            }
        }
        return node->tail() != nullptr && visit(*node->tail());
    }
    if(const auto *node = dynamic_cast<const ast::IfExpr *>(&expression)){
        return visit(node->condition()) || visit(node->thenBranch()) || (node->elseBranch() != nullptr && visit(*node->elseBranch()));
    }
    if(const auto *node = dynamic_cast<const ast::LoopExpr *>(&expression)){
        return visit(node->body());
    }
    if(const auto *node = dynamic_cast<const ast::WhileExpr *>(&expression)){
        return visit(node->condition()) || visit(node->body());
    }
    if(const auto *node = dynamic_cast<const ast::ReturnExpr *>(&expression)){
        return node->value() != nullptr && visit(*node->value());
    }
    if(const auto *node = dynamic_cast<const ast::BreakExpr *>(&expression)){
        return node->value() != nullptr && visit(*node->value());
    }
    return false;
}

bool diverges(const ast::Expr &expression, const SemanticModel &model);

// 查找退出当前 loop 且操作数不发散的 break，包含不可达的 break。
bool hasCompletingBreak(const ast::Expr &expression, const SemanticModel &model){
    if(const auto *node = dynamic_cast<const ast::BreakExpr *>(&expression)){
        return node->value() == nullptr ||
               !diverges(*node->value(), model) ||
               hasCompletingBreak(*node->value(), model);
    }
    if(dynamic_cast<const ast::LoopExpr *>(&expression) != nullptr){
        return false;
    }
    if(const auto *node = dynamic_cast<const ast::WhileExpr *>(&expression)){
        // while 条件中的跳转属于外层循环，循环体中的跳转属于 while。
        return hasCompletingBreak(node->condition(), model);
    }
    return anyChild(expression, [&](const ast::Expr &child){
        return hasCompletingBreak(child, model);
    });
}

bool diverges(const ast::Expr &expression, const SemanticModel &model){
    const auto type = model.expressionTypes.find(expression.id());
    if(type != model.expressionTypes.end() && type->second == model.types.builtin(TypeKind::Never)){
        return true;
    }
    if(dynamic_cast<const ast::ReturnExpr *>(&expression) != nullptr ||
       dynamic_cast<const ast::BreakExpr *>(&expression) != nullptr ||
       dynamic_cast<const ast::ContinueExpr *>(&expression) != nullptr){
        return true;
    }
    if(const auto *node = dynamic_cast<const ast::BinaryExpr *>(&expression)){
        // 短路运算的右侧不一定执行。
        if(node->op() == "&&" || node->op() == "||"){
            return diverges(node->left(), model);
        }
    }
    if(const auto *node = dynamic_cast<const ast::IfExpr *>(&expression)){
        return diverges(node->condition(), model) || (node->elseBranch() != nullptr && diverges(node->thenBranch(), model) && diverges(*node->elseBranch(), model));
    }
    if(const auto *node = dynamic_cast<const ast::WhileExpr *>(&expression)){
        return diverges(node->condition(), model);
    }
    if(const auto *node = dynamic_cast<const ast::LoopExpr *>(&expression)){
        return !hasCompletingBreak(node->body(), model);
    }
    return anyChild(expression, [&](const ast::Expr &child){
        return diverges(child, model);
    });
}

}

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
    const TypeId unitType = checker_.model_.types.builtin(TypeKind::Unit);
    const TypeId errorType = checker_.model_.types.builtin(TypeKind::Error);
    if(dynamic_cast<const rx::ast::EmptyStmt *>(&stmt) != nullptr){
        return unitType;
    }
    if(const auto *binding = dynamic_cast<const rx::ast::LetStmt *>(&stmt)){
        collectLetStmt(*binding);
        const auto symbol = checker_.model_.declaredSymbols.find(binding->id());
        if(symbol == checker_.model_.declaredSymbols.end()) return errorType;
        return checker_.model_.symbolTypes.at(symbol->second) == errorType ? errorType : unitType;
    }
    if(const auto *expression = dynamic_cast<const rx::ast::ExprStmt *>(&stmt)){
        return collectExprStmt(*expression);
    }
    checker_.diagnose(stmt, "unsupported statement");
    return errorType;
}

void StatementCollector::collectLetStmt(const rx::ast::LetStmt &stmt){
    // 先检查初始化式，再确定或验证绑定类型，最后创建局部符号并加入当前作用域。
    const TypeId errorType = checker_.model_.types.builtin(TypeKind::Error);
    std::optional<TypeId> expectedType;
    if(stmt.type() != nullptr){
        expectedType = checker_.resolution_.resolveType(*stmt.type());
    }

    const TypeId initializerType = checker_.expressionCollector_.collectExpr(stmt.initializer(), expectedType);
    TypeId type = expectedType.has_value() ? *expectedType : initializerType;
    if(initializerType == errorType) type = errorType;

    const std::string &name = stmt.name();
    if(name == "get_i32" || name == "print_i32" || name == "println_i32"){
        checker_.diagnose(stmt, "cannot shadow builtin value '" + name + "'");
        return;
    }

    const auto previous = checker_.scope_.lookupValue(name);
    if(previous.has_value() && checker_.model_.symbols.at(*previous).kind == SymbolKind::Constant){
        checker_.diagnose(stmt, "cannot shadow constant '" + name + "'");
        return;
    }

    const auto declared = checker_.model_.declaredSymbols.find(stmt.id());
    const SymbolId symbol = declared == checker_.model_.declaredSymbols.end() ? checker_.allocateSymbol() : declared->second;
    if(!checker_.scope_.declareValue(name, symbol, true)){
        checker_.diagnose(stmt, "cannot declare local binding '" + name + "'");
        return;
    }
    // 类型出错时仍建立 Error 绑定，减少后续未知名字诊断
    checker_.model_.symbols.insert_or_assign(symbol, SymbolInfo{name, SymbolKind::Local, stmt.id(), stmt.sourceSpan(), stmt.isMutable()});
    checker_.model_.declaredSymbols.insert_or_assign(stmt.id(), symbol);
    checker_.model_.symbolTypes.insert_or_assign(symbol, type);
}

TypeId StatementCollector::collectExprStmt(const rx::ast::ExprStmt &stmt){
    // 检查内部表达式，并依据是否带分号返回表达式类型或 unit
    const TypeId errorType = checker_.model_.types.builtin(TypeKind::Error);
    const TypeId unitType = checker_.model_.types.builtin(TypeKind::Unit);
    const TypeId type = checker_.expressionCollector_.collectExpr(stmt.expression());
    if(type == errorType) return errorType;
    if(!stmt.hasSemicolon() && !checker_.typesChecker_.canCoerce(type, unitType)){
        checker_.diagnose(stmt, "expression statement without semicolon must have type () or !");
        return errorType;
    }

    // 语句丢弃表达式结果；发散性由 diverges 单独保留
    return unitType;
}
