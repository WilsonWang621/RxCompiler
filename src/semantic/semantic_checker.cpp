#include "semantic_checker.hpp"

#include <limits>
#include <stdexcept>
#include <utility>

SemanticModel SemanticChecker::check(const rx::ast::Crate &crate){
    model_ = SemanticModel{};
    scope_ = Scope{};
    builtinSymbols_.clear();
    currentReturnType_.reset();
    currentSelfType_.reset();
    constantStates_.clear();
    nextSymbolId_ = 1;

    firstScan(crate);
    secondScan(crate);

    return std::move(model_);
}

SymbolId SemanticChecker::allocateSymbol(){
    if(nextSymbolId_ == 0 || nextSymbolId_ == std::numeric_limits<std::uint32_t>::max()){
        throw std::overflow_error("too many semantic symbols");
    }
    return SymbolId{nextSymbolId_++};
}

void SemanticChecker::diagnose(const rx::ast::ASTNode &node, std::string message){
    model_.diagnostics.push_back(Diagnosis{node.sourceSpan(), std::move(message)});
}

void SemanticChecker::firstScan(const rx::ast::Crate &crate){
    // 先建立完整的顶层命名空间，允许前向引用。
    for(const auto &item : crate.items()){
        if(const auto *function = dynamic_cast<const rx::ast::FunctionItem *>(item.get())){
            declarationCollector_.collectFunction(*function);
        }else if(const auto *constant = dynamic_cast<const rx::ast::ConstItem *>(item.get())){
            declarationCollector_.collectConstant(*constant);
        }else if(const auto *structure = dynamic_cast<const rx::ast::StructItem *>(item.get())){
            declarationCollector_.collectStruct(*structure);
        }
    }

    // impl 依赖完整的类型命名空间，因此在普通顶层声明之后收集。
    for(const auto &item : crate.items()){
        if(const auto *implementation = dynamic_cast<const rx::ast::ImplItem *>(item.get())){
            declarationCollector_.collectImpl(*implementation);
        }
    }
}

void SemanticChecker::secondScan(const rx::ast::Crate &crate){
    // 建立全局作用域，依次解析声明类型、求值常量、检查结构体和函数体，最后校验 main。
    declarationCollector_.prepareGlobalScope();
    declarationCollector_.collectDeclarationType(crate);
    for(const auto &[symbol, declaration] : model_.declarations.constants){
        constEvaluate_.evaluateConstant(symbol);
    }

    for(const auto &item : crate.items()){
        if(const auto *structure = dynamic_cast<const rx::ast::StructItem *>(item.get())){
            typesChecker_.checkStructLayout(*structure);
        }
    }
    for(const auto &item : crate.items()){
        if(const auto *structure = dynamic_cast<const rx::ast::StructItem *>(item.get())){
            typesChecker_.checkStructDerive(*structure);
        }
    }

    for(const auto &item : crate.items()){
        if(const auto *function = dynamic_cast<const rx::ast::FunctionItem *>(item.get())){
            functionCollector_.collectFunctionBody(*function);
        }else if(const auto *implementation = dynamic_cast<const rx::ast::ImplItem *>(item.get())){
            functionCollector_.collectImplBodies(*implementation);
        }
    }

    checkMain(crate);
}

void SemanticChecker::checkMain(const rx::ast::Crate &crate){
    // 顶层重名已由第一遍诊断；关联函数 main 不能充当程序入口。
    const auto symbol = model_.declarations.globalValues.find("main");
    if(symbol == model_.declarations.globalValues.end()){
        diagnose(crate, "missing top-level main function");
        return;
    }
    const auto declaration = model_.declarations.functions.find(symbol->second);
    if(declaration == model_.declarations.functions.end()){
        const auto &info = model_.symbols.at(symbol->second);
        model_.diagnostics.push_back(Diagnosis{info.span, "main must be a top-level function"});
        return;
    }

    const auto &function = *declaration->second.declaration;
    if(function.selfParam() != nullptr || !function.parameters().empty()){
        diagnose(function, "main must have no parameters or receiver");
    }
    if(function.hasGenericParameters()){
        diagnose(function, "main must have no generic parameters");
    }

    // 声明类型已统一解析，入口检查直接使用签名中的返回类型。
    const TypeId returnType = declaration->second.returnType;
    if(returnType.value == 0) throw std::logic_error("main signature must be collected before checking entry");

    const auto kind = model_.types.get(returnType).kind();
    if(kind != TypeKind::Unit && kind != TypeKind::Error){
        diagnose(function, "main must return unit ()");
    }
}
