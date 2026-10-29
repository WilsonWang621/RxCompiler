#include "declaration_generator.hpp"

#include <stdexcept>

void DeclarationGenerator::declareItem(const rx::ast::Item &item, IRGenerationContext &context){
    if(const auto *constant = dynamic_cast<const rx::ast::ConstItem *>(&item)){
        declareConstant(*constant, context);
        return;
    }
    if(const auto *structure = dynamic_cast<const rx::ast::StructItem *>(&item)){
        declareStruct(*structure, context);
        return;
    }
    if(const auto *function = dynamic_cast<const rx::ast::FunctionItem *>(&item)){
        declareFunction(*function, context);
        return;
    }
    if(const auto *implementation = dynamic_cast<const rx::ast::ImplItem *>(&item)){
        for(const auto &associated : implementation->items()){
            declareItem(*associated, context);
        }
        return;
    }
    throw std::runtime_error("unsupported item in IR declaration pass");
}

void DeclarationGenerator::declareStruct(const rx::ast::StructItem &item, IRGenerationContext &){
    // TODO: 登记命名结构体，按 AST 顺序建立字段布局。
    throw std::runtime_error("IR struct declaration is not implemented: " + item.name());
}

void DeclarationGenerator::declareConstant(const rx::ast::ConstItem &item, IRGenerationContext &){
    // TODO: 读取 SemanticModel 中的常量值，按 SymbolId 缓存。
    throw std::runtime_error("IR constant declaration is not implemented: " + item.name());
}

void DeclarationGenerator::declareFunction(const rx::ast::FunctionItem &function, IRGenerationContext &){
    // TODO: 登记返回类型、self 和普通形参；此阶段不生成函数体。
    throw std::runtime_error("IR function declaration is not implemented: " + function.name());
}
