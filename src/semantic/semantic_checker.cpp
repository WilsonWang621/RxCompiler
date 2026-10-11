#include "semantic_checker.hpp"

#include <limits>
#include <stdexcept>
#include <utility>

SemanticModel SemanticChecker::check(const rx::ast::Crate &crate){
    model_ = SemanticModel{};
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
            collectFunction(*function);
        }else if(const auto *constant = dynamic_cast<const rx::ast::ConstItem *>(item.get())){
            collectConstant(*constant);
        }else if(const auto *structure = dynamic_cast<const rx::ast::StructItem *>(item.get())){
            collectStruct(*structure);
        }
    }

    // impl 依赖完整的类型命名空间，因此在普通顶层声明之后收集。
    for(const auto &item : crate.items()){
        if(const auto *implementation = dynamic_cast<const rx::ast::ImplItem *>(item.get())){
            collectImpl(*implementation);
        }
    }
}

void SemanticChecker::collectFunction(const rx::ast::FunctionItem &function){
    const SymbolId symbol = allocateSymbol();
    const bool inserted = model_.declarations.globalValues.emplace(function.name(), symbol).second;
    if(!inserted){
        diagnose(function, "duplicate value declaration '" + function.name() + "'");
        return;
    }

    model_.symbols.emplace(symbol, SymbolInfo{function.name(), SymbolKind::Function, function.id(), function.sourceSpan(), false});
    model_.declaredSymbols.emplace(function.id(), symbol);

    FunctionDeclarationInfo declaration{symbol, &function};
    for(std::size_t index = 0; index < function.parameters().size(); ++index){
        const auto *parameter = dynamic_cast<const rx::ast::NamedFunctionParam *>(function.parameters()[index].get());
        if(parameter == nullptr) continue;

        if(!declaration.parameters.emplace(parameter->name(), index).second){
            diagnose(*parameter, "duplicate parameter '" + parameter->name() + "'");
        }
    }
    model_.declarations.functions.emplace(symbol, std::move(declaration));
}

void SemanticChecker::collectConstant(const rx::ast::ConstItem &constant){
    const SymbolId symbol = allocateSymbol();
    const bool inserted = model_.declarations.globalValues.emplace(constant.name(), symbol).second;
    if(!inserted){
        diagnose(constant, "duplicate value declaration '" + constant.name() + "'");
        return;
    }

    model_.symbols.emplace(symbol, SymbolInfo{constant.name(), SymbolKind::Constant, constant.id(), constant.sourceSpan(), false});
    model_.declaredSymbols.emplace(constant.id(), symbol);
    model_.declarations.constants.emplace(symbol, ConstantDeclarationInfo{symbol, &constant});
}

void SemanticChecker::collectStruct(const rx::ast::StructItem &structure){
    const SymbolId symbol = allocateSymbol();
    const bool inserted = model_.declarations.globalTypes.emplace(structure.name(), symbol).second;
    if(!inserted){
        diagnose(structure, "duplicate type declaration '" + structure.name() + "'");
        return;
    }

    model_.symbols.emplace(symbol, SymbolInfo{structure.name(), SymbolKind::Struct, structure.id(), structure.sourceSpan(), false});
    model_.declaredSymbols.emplace(structure.id(), symbol);

    StructDeclarationInfo declaration{symbol, &structure};
    for(const auto &field : structure.fields()){
        if(!declaration.fields.emplace(field->name(), field.get()).second){
            diagnose(*field, "duplicate field '" + field->name() + "' in struct '" + structure.name() + "'");
        }
    }
    model_.declarations.structs.emplace(symbol, std::move(declaration));
}

void SemanticChecker::collectImpl(const rx::ast::ImplItem &implementation){
    const auto *target = dynamic_cast<const rx::ast::TypePathRef *>(&implementation.type());
    if(target == nullptr || target->segments().size() != 1 || target->segments().front()->hasGenericArgs()){
        diagnose(implementation, "impl target must be a non-generic user struct");
        return;
    }

    const std::string &typeName = target->segments().front()->name();
    const auto type = model_.declarations.globalTypes.find(typeName);
    if(type == model_.declarations.globalTypes.end()){
        diagnose(implementation, "unknown impl target '" + typeName + "'");
        return;
    }

    auto structure = model_.declarations.structs.find(type->second);
    if(structure == model_.declarations.structs.end()){
        diagnose(implementation, "impl target '" + typeName + "' is not a struct");
        return;
    }

    structure->second.implBlocks.push_back(&implementation);
    for(const auto &item : implementation.items()){
        const auto *function = dynamic_cast<const rx::ast::FunctionItem *>(item.get());
        const auto *constant = dynamic_cast<const rx::ast::ConstItem *>(item.get());
        if(function == nullptr && constant == nullptr){
            diagnose(*item, "unsupported item in impl block");
            continue;
        }

        const std::string &name = function != nullptr ? function->name() : constant->name();
        const SymbolId symbol = allocateSymbol();
        if(!structure->second.associatedValues.emplace(name, symbol).second){
            diagnose(*item, "duplicate associated value '" + name + "' for struct '" + typeName + "'");
            continue;
        }

        const SymbolKind kind = function != nullptr ? SymbolKind::Function : SymbolKind::Constant;
        model_.symbols.emplace(symbol, SymbolInfo{name, kind, item->id(), item->sourceSpan(), false});
        model_.declaredSymbols.emplace(item->id(), symbol);

        if(function != nullptr){
            FunctionDeclarationInfo declaration{symbol, function};
            for(std::size_t index = 0; index < function->parameters().size(); ++index){
                const auto *parameter = dynamic_cast<const rx::ast::NamedFunctionParam *>(function->parameters()[index].get());
                if(parameter == nullptr) continue;
                if(!declaration.parameters.emplace(parameter->name(), index).second){
                    diagnose(*parameter, "duplicate parameter '" + parameter->name() + "'");
                }
            }
            model_.declarations.functions.emplace(symbol, std::move(declaration));
        }else{
            model_.declarations.constants.emplace(symbol, ConstantDeclarationInfo{symbol, constant});
        }
    }
}

void SemanticChecker::secondScan(const rx::ast::Crate &crate){
    // 类型表示和表达式检查器尚未实现；此阶段暂不写入未经验证的结果。
    (void)crate;
}
