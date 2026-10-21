#include "function_collector.hpp"
#include "semantic_checker.hpp"

#include <algorithm>
#include <stdexcept>

void FunctionCollector::collectFunctionBody(const rx::ast::FunctionItem &function, std::optional<TypeId> selfType){
    const auto symbol = checker_.model_.declaredSymbols.find(function.id());
    if(symbol == checker_.model_.declaredSymbols.end()) return;
    const TypeId returnType = checker_.model_.declarations.functions.at(symbol->second).returnType;
    if(returnType.value == 0) throw std::logic_error("function signature must be collected before its body");

    ScopeGuard functionScope(checker_.scope_);
    // 与作用域一起恢复上下文，包含检查函数体期间抛出异常的情况。
    struct ContextGuard{
        std::optional<TypeId> &returnType;
        std::optional<TypeId> &selfType;
        std::optional<TypeId> previousReturnType;
        std::optional<TypeId> previousSelfType;

        ~ContextGuard(){
            returnType = previousReturnType;
            selfType = previousSelfType;
        }
    } context{checker_.currentReturnType_, checker_.currentSelfType_, checker_.currentReturnType_, checker_.currentSelfType_};

    checker_.currentReturnType_ = returnType;
    checker_.currentSelfType_ = selfType;
    collectFunctionParameters(function, selfType);
    // 参数和函数最外层的局部绑定使用同一层作用域；预期类型交给块检查器验证。
    checker_.statementCollector_.collectBlockContents(function.body(), returnType);
}

void FunctionCollector::collectFunctionParameters(const rx::ast::FunctionItem &function, std::optional<TypeId> selfType){
    const auto symbol = checker_.model_.declaredSymbols.find(function.id());
    if(symbol == checker_.model_.declaredSymbols.end()) return;
    const auto &declaration = checker_.model_.declarations.functions.at(symbol->second);
    if(declaration.parameterTypes.size() != function.parameters().size()){
        throw std::logic_error("parameter types must be collected before parameter bindings");
    }

    if(const auto *receiver = function.selfParam()){
        // 无 impl 上下文的 self 已在签名检查时报告，不重复诊断。
        if(selfType.has_value()) collectSelfParameter(*receiver, *selfType);
    }
    for(std::size_t index = 0; index < function.parameters().size(); ++index){
        const auto *parameter = dynamic_cast<const rx::ast::NamedFunctionParam *>(function.parameters()[index].get());
        if(parameter == nullptr) continue;
        const auto position = declaration.parameters.find(parameter->name());
        // 第一遍只保留第一个同名参数，后面的重复参数不覆盖它的绑定。
        if(position == declaration.parameters.end() || position->second != index) continue;

        const TypeId type = declaration.parameterTypes[index];
        checker_.model_.types.get(type);
        checker_.model_.resolvedTypes.insert_or_assign(parameter->type().id(), type);
        collectParameter(*parameter);
    }
}

void FunctionCollector::collectParameter(const rx::ast::NamedFunctionParam &parameter){
    const TypeId type = checker_.model_.resolvedTypes.at(parameter.type().id());
    checker_.model_.types.get(type);
    const auto declared = checker_.model_.declaredSymbols.find(parameter.id());
    const SymbolId symbol = declared == checker_.model_.declaredSymbols.end() ? checker_.allocateSymbol() : declared->second;
    if(!checker_.scope_.declareValue(parameter.name(), symbol)){
        checker_.diagnose(parameter, "duplicate parameter '" + parameter.name() + "'");
        return;
    }

    checker_.model_.symbols.insert_or_assign(symbol, SymbolInfo{parameter.name(), SymbolKind::Parameter,parameter.id(), parameter.sourceSpan(), parameter.isMutable()});
    checker_.model_.declaredSymbols.insert_or_assign(parameter.id(), symbol);
    checker_.model_.symbolTypes.insert_or_assign(symbol, type);
}

void FunctionCollector::collectSelfParameter(const rx::ast::SelfFunctionParam &parameter, TypeId selfType){
    if(checker_.model_.types.get(selfType).kind() != TypeKind::Struct){
        checker_.diagnose(parameter, "self parameter owner must be a struct");
        return;
    }
    const TypeId type = parameter.isReference() ? checker_.model_.types.reference(selfType, parameter.isMutable()) : selfType;
    const auto declared = checker_.model_.declaredSymbols.find(parameter.id());
    const SymbolId symbol = declared == checker_.model_.declaredSymbols.end() ? checker_.allocateSymbol() : declared->second;
    if(!checker_.scope_.declareValue("self", symbol)){
        checker_.diagnose(parameter, "duplicate self parameter");
        return;
    }

    // &mut self 的绑定本身不可重新赋值；可变引用属性由 type 保存。
    const bool isMutable = !parameter.isReference() && parameter.isMutable();
    checker_.model_.symbols.insert_or_assign(symbol, SymbolInfo{"self", SymbolKind::Receiver, parameter.id(), parameter.sourceSpan(), isMutable});
    checker_.model_.declaredSymbols.insert_or_assign(parameter.id(), symbol);
    checker_.model_.symbolTypes.insert_or_assign(symbol, type);
}

void FunctionCollector::collectImplBodies(const rx::ast::ImplItem &implementation){
    const auto target = checker_.model_.resolvedTypes.find(implementation.type().id());
    if(target == checker_.model_.resolvedTypes.end()) return;
    const TypeId selfType = target->second;
    const auto &type = checker_.model_.types.get(selfType);
    if(type.kind() != TypeKind::Struct) return;
    const auto structure = checker_.model_.declarations.structs.find(type.symbol());
    if(structure == checker_.model_.declarations.structs.end()) return;
    const auto &blocks = structure->second.implBlocks;
    if(std::find(blocks.begin(), blocks.end(), &implementation) == blocks.end()) return;

    // 常量求值也可能出现 Self::NAME；离开 impl 时恢复原有类型上下文。
    struct ContextGuard{
        std::optional<TypeId> &selfType;
        std::optional<TypeId> previousSelfType;
        ~ContextGuard(){ selfType = previousSelfType; }
    } context{checker_.currentSelfType_, checker_.currentSelfType_};
    checker_.currentSelfType_ = selfType;

    for(const auto &item : implementation.items()){
        const auto symbol = checker_.model_.declaredSymbols.find(item->id());
        if(symbol == checker_.model_.declaredSymbols.end()) continue;
        if(const auto *function = dynamic_cast<const rx::ast::FunctionItem *>(item.get())){
            collectFunctionBody(*function, selfType);
        }else if(dynamic_cast<const rx::ast::ConstItem *>(item.get()) != nullptr){
            if(checker_.model_.declarations.constants.at(symbol->second).type.value == 0){
                throw std::logic_error("constant type must be collected before its initializer");
            }
            checker_.constEvaluate_.evaluateConstant(symbol->second);
        }
    }
}
