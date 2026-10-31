#include "ir_generator.hpp"

#include <stdexcept>

void IRGenerator::generateFunction(const rx::ast::FunctionItem &function){
    if(!context_.model) throw std::logic_error("IR generation has no semantic model");
    if(context_.currentFunction.value != 0) throw std::logic_error("IR function generation is already active");
    const SymbolId symbol = context_.model->declaredSymbols.at(function.id());
    IRFunction *irFunction = context_.functions.at(symbol);
    if(!irFunction) throw std::logic_error("IR function declaration is missing");

    context_.locals.clear();
    context_.currentFunction = symbol;
    struct FunctionGuard{
        IRGenerationContext &context;
        ~FunctionGuard(){
            context.locals.clear();
            context.currentFunction = {};
        }
    } guard{context_};

    auto &builder = context_.builder;
    builder.beginFunction(*irFunction);
    auto &entry = builder.createBlock(*irFunction, "entry");
    builder.setInsertPoint(entry);
    bindParameters(function);
    const IRExprResult result = generateBlock(function.body());
    if(result.terminated != builder.isTerminated() || (result.terminated && result.value.has_value())){
        throw std::logic_error("IR block result does not match its termination state");
    }
    if(!result.terminated) builder.createReturn(result.value);
}

void IRGenerator::bindParameters(const rx::ast::FunctionItem &function){
    const auto &model = *context_.model;
    const auto &irFunction = *context_.functions.at(context_.currentFunction);
    const std::size_t count = function.parameters().size() + (function.selfParam() ? 1 : 0);
    if(irFunction.parameters.size() != count) throw std::logic_error("IR parameter count does not match the function");

    std::size_t index = 0;
    const auto bindParameter = [&](const rx::ast::FunctionParam &parameter){
        const SymbolId symbol = model.declaredSymbols.at(parameter.id());
        const auto &incoming = irFunction.parameters.at(index++);
        const IRType type = context_.typeLowering.lowerType(model.symbolTypes.at(symbol), model);
        if(type != incoming.type) throw std::logic_error("IR parameter type does not match semantic type");
        const IRValue address = context_.builder.createAlloca(type);
        context_.builder.createStore(IRValue{type, incoming.name, 0}, address);
        if(!context_.locals.emplace(symbol, address).second) throw std::logic_error("duplicate IR parameter binding");
    };
    // self 和普通参数统一保存到栈槽；后续修改绑定不会改写 SSA 入参。
    if(function.selfParam()) bindParameter(*function.selfParam());
    for(const auto &parameter : function.parameters()){
        bindParameter(*parameter);
    }
}

void IRGenerator::generateMainWrapper(){
    if(!context_.model) throw std::logic_error("IR generation has no semantic model");
    if(context_.currentFunction.value != 0) throw std::logic_error("IR function generation is already active");
    const SymbolId symbol = context_.model->declarations.globalValues.at("main");
    const IRFunction *sourceMain = context_.functions.at(symbol);
    if(!sourceMain) throw std::logic_error("IR source main declaration is missing");
    if(sourceMain->returnType != IRType::Void || !sourceMain->parameters.empty()){
        throw std::logic_error("IR source main must have no parameters and return void");
    }
    if(sourceMain->isDeclaration) throw std::logic_error("IR source main body has not been generated");
    if(sourceMain->name == "main") throw std::logic_error("IR source main requires a distinct internal name");

    auto &builder = context_.builder;
    IRFunction &wrapper = builder.declareFunction("main", IRType::I32, {}, false);
    builder.beginFunction(wrapper);
    auto &entry = builder.createBlock(wrapper, "entry");
    builder.setInsertPoint(entry);
    // 保留 SymbolId 到源码 main 的映射，源码调用仍指向内部函数。
    builder.createCall(*sourceMain, {});
    builder.createReturn(IRValue{IRType::I32, "", 0});
}
