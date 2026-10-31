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

// 内置调用需要的最小表达式支持；完整运算和 place 生成仍待补充。
IRExprResult IRGenerator::generateExpr(const rx::ast::Expr &expression){
    const auto &model = *context_.model;
    if(const auto *call = dynamic_cast<const rx::ast::CallExpr *>(&expression)) return generateCall(*call);
    if(const auto *block = dynamic_cast<const rx::ast::BlockExpr *>(&expression)) return generateBlock(*block);
    if(const auto *ret = dynamic_cast<const rx::ast::ReturnExpr *>(&expression)){
        const auto result = ret->value() ? generateExpr(*ret->value()) : IRExprResult{};
        if(!result.terminated) context_.builder.createReturn(result.value);
        return {{}, true};
    }
    if(dynamic_cast<const rx::ast::UnitExpr *>(&expression)) return {};
    const auto constant = model.constantValues.find(expression.id());
    if(constant != model.constantValues.end()){
        return {IRValue{context_.typeLowering.lowerType(constant->second.type, model), "", constant->second.bits}, false};
    }
    if(const auto *path = dynamic_cast<const rx::ast::PathExpr *>(&expression)) return generatePath(*path);
    if(const auto *unary = dynamic_cast<const rx::ast::UnaryExpr *>(&expression)){
        // 字面量的负号由位模式求值，包含 i32::MIN；不解析字面量文本。
        const auto operand = generateExpr(unary->operand());
        if(operand.terminated) return operand;
        if(unary->op() == "-" && operand.value && operand.value->name.empty()){
            auto value = *operand.value;
            value.bits = 0u - value.bits;
            return {value, false};
        }
    }
    throw std::runtime_error("IR expression is not implemented");
}

IRExprResult IRGenerator::generatePath(const rx::ast::PathExpr &expression){
    const auto &model = *context_.model;
    const SymbolId symbol = model.resolvedSymbols.at(expression.id());
    const auto constant = context_.constants.find(symbol);
    if(constant != context_.constants.end()) return {constant->second, false};
    const IRType type = context_.typeLowering.lowerType(model.symbolTypes.at(symbol), model);
    if(type == IRType::Void) return {};
    return {context_.builder.createLoad(type, context_.locals.at(symbol)), false};
}

IRExprResult IRGenerator::generateCall(const rx::ast::CallExpr &expression){
    const auto &model = *context_.model;
    const SymbolId symbol = model.resolvedSymbols.at(expression.id());
    const auto &callee = *context_.functions.at(symbol);
    std::vector<IRValue> arguments;
    for(const auto &argument : expression.arguments()){
        const auto result = generateExpr(*argument);
        if(result.terminated) return result;
        if(!result.value) throw std::runtime_error("IR unit arguments are not supported");
        arguments.push_back(*result.value);
    }
    return {context_.builder.createCall(callee, std::move(arguments)), false};
}
