#include "ir_generator.hpp"

#include <stdexcept>

IRExprResult IRGenerator::generateBlock(const rx::ast::BlockExpr &block){
    for(const auto &statement : block.statements()){
        const auto result = generateStmt(*statement);
        if(result.terminated) return result;
    }
    return block.tail() ? generateExpr(*block.tail()) : IRExprResult{};
}

IRExprResult IRGenerator::generateStmt(const rx::ast::Stmt &stmt){
    if(const auto *expression = dynamic_cast<const rx::ast::ExprStmt *>(&stmt)){
        return generateExpr(expression->expression());
    }
    if(const auto *binding = dynamic_cast<const rx::ast::LetStmt *>(&stmt)){
        generateLet(*binding);
        return {{}, context_.builder.isTerminated()};
    }
    if(dynamic_cast<const rx::ast::EmptyStmt *>(&stmt)) return {};
    throw std::runtime_error("IR statement is not implemented");
}

void IRGenerator::generateLet(const rx::ast::LetStmt &stmt){
    const auto result = generateExpr(stmt.initializer());
    if(result.terminated || !result.value) return;
    const SymbolId symbol = context_.model->declaredSymbols.at(stmt.id());
    const auto address = context_.builder.createAlloca(result.value->type);
    context_.builder.createStore(*result.value, address);
    context_.locals.emplace(symbol, address);
}
