#include "ir_generator.hpp"

#include <stdexcept>

void IRGenerator::generateItem(const rx::ast::Item &item, IRGenerationContext &context){
    if(const auto *function = dynamic_cast<const rx::ast::FunctionItem *>(&item)){
        functionGenerator_.generateFunction(*function, context);
        return;
    }
    if(const auto *implementation = dynamic_cast<const rx::ast::ImplItem *>(&item)){
        for(const auto &associated : implementation->items()){
            generateItem(*associated, context);
        }
        return;
    }
    // 常量和结构体已由第一遍登记，不在此处生成运行时指令。
    if(dynamic_cast<const rx::ast::ConstItem *>(&item) ||
       dynamic_cast<const rx::ast::StructItem *>(&item)) return;
    throw std::runtime_error("unsupported item in IR body pass");
}

IRModule IRGenerator::generate(const rx::ast::Crate &crate, const SemanticModel &model){
    if(!model.diagnostics.empty()) throw std::invalid_argument("IR generation requires valid semantics");
    IRModule module;
    context_.reset(module, model);
    try{
        for(const auto &item : crate.items()){
            declarationGenerator_.declareItem(*item, context_);
        }
        for(const auto &item : crate.items()){
            generateItem(*item, context_);
        }
        functionGenerator_.generateMainWrapper(context_);
        context_.clear();
        return module;
    }catch(...){
        context_.clear();
        throw;
    }
}
