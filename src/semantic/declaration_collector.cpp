#include "declaration_collector.hpp"
#include "semantic_checker.hpp"

#include <algorithm>
#include <initializer_list>
#include <utility>

void DeclarationCollector::prepareGlobalScope(){
    checker_.scope_ = Scope{};

    // 内建符号没有源码声明；重复准备作用域时复用符号，不重新分配 ID。
    auto declareBuiltin = [&](const std::string &name, bool isType){
        auto builtin = checker_.builtinSymbols_.find(name);
        if(builtin == checker_.builtinSymbols_.end()){
            const SymbolId symbol = checker_.allocateSymbol();
            checker_.model_.symbols.emplace(symbol, SymbolInfo{name, SymbolKind::Builtin, 0, {}, false});
            builtin = checker_.builtinSymbols_.emplace(name, symbol).first;
        }

        if(isType){
            checker_.scope_.declareType(name, builtin->second);
        }else{
            checker_.scope_.declareValue(name, builtin->second);
        }
    };

    for(const char *name : {"i32", "u32", "isize", "usize", "bool", "Box", "Vec",
                           "Copy", "Clone", "PartialEq", "Eq"}){
        declareBuiltin(name, true);
    }
    for(const char *name : {"get_i32", "print_i32", "println_i32"}){
        declareBuiltin(name, false);
    }

    // 全局表只包含顶层声明；关联项、字段和局部绑定不加入全局作用域。
    for(const auto &[name, symbol] : checker_.model_.declarations.globalValues){
        if(!checker_.scope_.declareValue(name, symbol)){
            const auto &info = checker_.model_.symbols.at(symbol);
            checker_.model_.diagnostics.push_back(Diagnosis{info.span, "cannot redeclare builtin value '" + name + "'"});
        }
    }
    for(const auto &[name, symbol] : checker_.model_.declarations.globalTypes){
        if(!checker_.scope_.declareType(name, symbol)){
            const auto &info = checker_.model_.symbols.at(symbol);
            checker_.model_.diagnostics.push_back(Diagnosis{info.span, "cannot redeclare builtin type '" + name + "'"});
        }
    }
}

void DeclarationCollector::collectFunction(const rx::ast::FunctionItem &function){
    const SymbolId symbol = checker_.allocateSymbol();
    const bool inserted = checker_.model_.declarations.globalValues.emplace(function.name(), symbol).second;
    if(!inserted){
        checker_.diagnose(function, "duplicate value declaration '" + function.name() + "'");
        return;
    }

    checker_.model_.symbols.emplace(symbol, SymbolInfo{function.name(), SymbolKind::Function, function.id(), function.sourceSpan(), false});
    checker_.model_.declaredSymbols.emplace(function.id(), symbol);

    FunctionDeclarationInfo declaration{symbol, &function};
    for(std::size_t index = 0; index < function.parameters().size(); ++index){
        const auto *parameter = dynamic_cast<const rx::ast::NamedFunctionParam *>(function.parameters()[index].get());
        if(parameter == nullptr) continue;

        if(!declaration.parameters.emplace(parameter->name(), index).second){
            checker_.diagnose(*parameter, "duplicate parameter '" + parameter->name() + "'");
        }
    }
    checker_.model_.declarations.functions.emplace(symbol, std::move(declaration));
}

void DeclarationCollector::collectConstant(const rx::ast::ConstItem &constant){
    const SymbolId symbol = checker_.allocateSymbol();
    const bool inserted = checker_.model_.declarations.globalValues.emplace(constant.name(), symbol).second;
    if(!inserted){
        checker_.diagnose(constant, "duplicate value declaration '" + constant.name() + "'");
        return;
    }

    checker_.model_.symbols.emplace(symbol, SymbolInfo{constant.name(), SymbolKind::Constant, constant.id(), constant.sourceSpan(), false});
    checker_.model_.declaredSymbols.emplace(constant.id(), symbol);
    checker_.model_.declarations.constants.emplace(symbol, ConstantDeclarationInfo{symbol, &constant});
}

void DeclarationCollector::collectStruct(const rx::ast::StructItem &structure){
    const SymbolId symbol = checker_.allocateSymbol();
    const bool inserted = checker_.model_.declarations.globalTypes.emplace(structure.name(), symbol).second;
    if(!inserted){
        checker_.diagnose(structure, "duplicate type declaration '" + structure.name() + "'");
        return;
    }

    checker_.model_.symbols.emplace(symbol, SymbolInfo{structure.name(), SymbolKind::Struct, structure.id(), structure.sourceSpan(), false});
    checker_.model_.declaredSymbols.emplace(structure.id(), symbol);

    StructDeclarationInfo declaration{symbol, &structure};
    for(const auto &field : structure.fields()){
        if(!declaration.fields.emplace(field->name(), field.get()).second){
            checker_.diagnose(*field, "duplicate field '" + field->name() + "' in struct '" + structure.name() + "'");
        }
    }
    checker_.model_.declarations.structs.emplace(symbol, std::move(declaration));
}

void DeclarationCollector::collectImpl(const rx::ast::ImplItem &implementation){
    const auto *target = dynamic_cast<const rx::ast::TypePathRef *>(&implementation.type());
    if(target == nullptr || target->segments().size() != 1 || target->segments().front()->hasGenericArgs()){
        checker_.diagnose(implementation, "impl target must be a non-generic user struct");
        return;
    }

    const std::string &typeName = target->segments().front()->name();
    const auto type = checker_.model_.declarations.globalTypes.find(typeName);
    if(type == checker_.model_.declarations.globalTypes.end()){
        checker_.diagnose(implementation, "unknown impl target '" + typeName + "'");
        return;
    }

    auto structure = checker_.model_.declarations.structs.find(type->second);
    if(structure == checker_.model_.declarations.structs.end()){
        checker_.diagnose(implementation, "impl target '" + typeName + "' is not a struct");
        return;
    }

    structure->second.implBlocks.push_back(&implementation);
    for(const auto &item : implementation.items()){
        const auto *function = dynamic_cast<const rx::ast::FunctionItem *>(item.get());
        const auto *constant = dynamic_cast<const rx::ast::ConstItem *>(item.get());
        if(function == nullptr && constant == nullptr){
            checker_.diagnose(*item, "unsupported item in impl block");
            continue;
        }

        const std::string &name = function != nullptr ? function->name() : constant->name();
        const SymbolId symbol = checker_.allocateSymbol();
        if(!structure->second.associatedValues.emplace(name, symbol).second){
            checker_.diagnose(*item, "duplicate associated value '" + name + "' for struct '" + typeName + "'");
            continue;
        }

        const SymbolKind kind = function != nullptr ? SymbolKind::Function : SymbolKind::Constant;
        checker_.model_.symbols.emplace(symbol, SymbolInfo{name, kind, item->id(), item->sourceSpan(), false});
        checker_.model_.declaredSymbols.emplace(item->id(), symbol);

        if(function != nullptr){
            FunctionDeclarationInfo declaration{symbol, function};
            for(std::size_t index = 0; index < function->parameters().size(); ++index){
                const auto *parameter = dynamic_cast<const rx::ast::NamedFunctionParam *>(function->parameters()[index].get());
                if(parameter == nullptr) continue;
                if(!declaration.parameters.emplace(parameter->name(), index).second){
                    checker_.diagnose(*parameter, "duplicate parameter '" + parameter->name() + "'");
                }
            }
            checker_.model_.declarations.functions.emplace(symbol, std::move(declaration));
        }else{
            ConstantDeclarationInfo declaration{symbol, constant};
            declaration.selfType = checker_.model_.types.structure(structure->first);
            checker_.model_.declarations.constants.emplace(symbol, std::move(declaration));
        }
    }
}

void DeclarationCollector::collectFunctionSignature(const rx::ast::FunctionItem &function, std::optional<TypeId> selfType){
    const auto symbol = checker_.model_.declaredSymbols.find(function.id());
    if(symbol == checker_.model_.declaredSymbols.end()) return; // 跳过第一遍已拒绝的重复声明。

    auto &declaration = checker_.model_.declarations.functions.at(symbol->second);
    declaration.parameterTypes.clear();
    declaration.parameterTypes.reserve(function.parameters().size());
    declaration.receiverType.reset();
    const TypeId errorType = checker_.model_.types.builtin(TypeKind::Error);

    for(const auto &parameter : function.parameters()){
        const auto *named = dynamic_cast<const rx::ast::NamedFunctionParam *>(parameter.get());
        if(named == nullptr){
            checker_.diagnose(*parameter, "expected an ordinary named parameter");
            declaration.parameterTypes.push_back(errorType);
        }else{
            declaration.parameterTypes.push_back(checker_.resolution_.resolveType(named->type(), selfType));
        }
    }

    if(const auto *receiver = function.selfParam()){
        if(!selfType.has_value()){
            checker_.diagnose(*receiver, "self parameter is only allowed in an impl function");
            declaration.receiverType = errorType;
        }else{
            declaration.receiverType = receiver->isReference() ? checker_.model_.types.reference(*selfType, receiver->isMutable()) : *selfType;
        }
    }

    declaration.returnType = function.returnType() != nullptr ? checker_.resolution_.resolveType(*function.returnType(), selfType) : checker_.model_.types.builtin(TypeKind::Unit);
}

void DeclarationCollector::collectConstantType(const rx::ast::ConstItem &constant, std::optional<TypeId> selfType){
    const auto symbol = checker_.model_.declaredSymbols.find(constant.id());
    if(symbol == checker_.model_.declaredSymbols.end()) return;

    auto &declaration = checker_.model_.declarations.constants.at(symbol->second);
    declaration.selfType = selfType;
    declaration.type = checker_.resolution_.resolveType(constant.type(), selfType);
}

void DeclarationCollector::collectStructField(const rx::ast::StructField &field, TypeId selfType){
    const auto &type = checker_.model_.types.get(selfType);
    if(type.kind() != TypeKind::Struct){
        checker_.diagnose(field, "field owner must be a struct");
        return;
    }

    auto &structure = checker_.model_.declarations.structs.at(type.symbol());
    const auto declaredField = structure.fields.find(field.name());
    // 重名字段不能覆盖第一遍保留的字段类型，也不能写入其他结构体的字段。
    if(declaredField == structure.fields.end() || declaredField->second != &field) return;

    structure.fieldTypes.insert_or_assign(field.name(), checker_.resolution_.resolveType(field.type(), selfType));
}

void DeclarationCollector::collectImplTypes(const rx::ast::ImplItem &implementation){
    // 第一遍已校验 impl 目标；这里复用声明身份，不重复报告非法目标。
    const auto *target = dynamic_cast<const rx::ast::TypePathRef *>(&implementation.type());
    if(target == nullptr || target->segments().size() != 1 || target->segments().front()->hasGenericArgs()) return;

    const auto symbol = checker_.model_.declarations.globalTypes.find(target->segments().front()->name());
    if(symbol == checker_.model_.declarations.globalTypes.end()) return;
    const auto structure = checker_.model_.declarations.structs.find(symbol->second);
    if(structure == checker_.model_.declarations.structs.end()) return;

    const auto &blocks = structure->second.implBlocks;
    if(std::find(blocks.begin(), blocks.end(), &implementation) == blocks.end()) return;

    const TypeId selfType = checker_.model_.types.structure(symbol->second);
    checker_.model_.resolvedTypes.insert_or_assign(implementation.type().id(), selfType);
    for(const auto &item : implementation.items()){
        if(const auto *function = dynamic_cast<const rx::ast::FunctionItem *>(item.get())){
            collectFunctionSignature(*function, selfType);
        }else if(const auto *constant = dynamic_cast<const rx::ast::ConstItem *>(item.get())){
            collectConstantType(*constant, selfType);
        }
    }
}
