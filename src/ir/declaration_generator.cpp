#include "ir_generator.hpp"

#include <stdexcept>
#include <utility>

void IRGenerator::declareItem(const rx::ast::Item &item){
    if(const auto *constant = dynamic_cast<const rx::ast::ConstItem *>(&item)){
        declareConstant(*constant);
        return;
    }
    if(const auto *structure = dynamic_cast<const rx::ast::StructItem *>(&item)){
        declareStruct(*structure);
        return;
    }
    if(const auto *function = dynamic_cast<const rx::ast::FunctionItem *>(&item)){
        declareFunction(*function);
        return;
    }
    if(const auto *implementation = dynamic_cast<const rx::ast::ImplItem *>(&item)){
        for(const auto &associated : implementation->items()){
            declareItem(*associated);
        }
        return;
    }
    throw std::runtime_error("unsupported item in IR declaration pass");
}

void IRGenerator::declareStruct(const rx::ast::StructItem &item){
    if(!context_.model) throw std::logic_error("IR generation has no semantic model");
    const auto &model = *context_.model;
    const SymbolId symbol = model.declaredSymbols.at(item.id());
    if(context_.structs.count(symbol)) throw std::logic_error("IR struct is already declared");

    std::vector<IRType> fields;
    fields.reserve(item.fields().size());
    IRStructLayout layout;

    for(const auto &field : item.fields()){
        const TypeId type = model.resolvedTypes.at(field->type().id());
        fields.push_back(context_.typeLowering.lowerType(type, model));
        if(!layout.fieldIndices.emplace(field->name(), fields.size() - 1).second){
            throw std::logic_error("duplicate field in IR struct: " + field->name());
        }
    }
    // Module 拥有实体；Context 只保存查找映射，不改变 Builder 插入位置。
    layout.structure = &context_.builder.declareStruct(mangleSymbol(symbol, model), std::move(fields));
    context_.structs.emplace(symbol, std::move(layout));
}

std::string IRGenerator::mangleSymbol(SymbolId symbol, const SemanticModel &model) const{
    if(symbol.value == 0) throw std::invalid_argument("invalid IR symbol");
    return "rx_" + std::to_string(symbol.value) + "_" + model.symbols.at(symbol).name;
}

void IRGenerator::declareConstant(const rx::ast::ConstItem &item){
    if(!context_.model) throw std::logic_error("IR generation has no semantic model");
    const auto &model = *context_.model;
    const SymbolId symbol = model.declaredSymbols.at(item.id());
    if(context_.constants.count(symbol)) throw std::logic_error("IR constant is already declared");

    const auto &value = model.constantValues.at(item.value().id());

    IRValue constant; // name 保持为空，表示立即数。
    constant.type = context_.typeLowering.lowerType(value.type, model);
    constant.bits = value.bits;

    context_.constants.emplace(symbol, std::move(constant));
}

void IRGenerator::declareFunction(const rx::ast::FunctionItem &function){
    if(!context_.model) throw std::logic_error("IR generation has no semantic model");
    const auto &model = *context_.model;
    const SymbolId symbol = model.declaredSymbols.at(function.id());
    if(context_.functions.count(symbol)) throw std::logic_error("IR function is already declared");

    const auto &declaration = model.declarations.functions.at(symbol);
    std::vector<IRParameter> parameters;
    parameters.reserve(declaration.parameterTypes.size() + (declaration.receiverType.has_value() ? 1 : 0));
    const auto addParameter = [&](TypeId type){
        parameters.push_back({context_.typeLowering.lowerType(type, model), "p" + std::to_string(parameters.size())});
    };
    // self 位于首位
    if(declaration.receiverType.has_value()) addParameter(*declaration.receiverType);
    for(TypeId type : declaration.parameterTypes){
        addParameter(type);
    }
    const IRType returnType = context_.typeLowering.lowerType(declaration.returnType, model);
    // 源码 main 也使用内部名称；机器入口由后续包装器生成。
    IRFunction &irFunction = context_.builder.declareFunction(mangleSymbol(symbol, model), returnType, std::move(parameters));
    context_.functions.emplace(symbol, &irFunction);
}
