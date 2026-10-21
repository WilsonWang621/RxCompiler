#include "resolution.hpp"
#include "semantic_checker.hpp"

#include <algorithm>
#include <utility>

TypeId Resolution::resolveType(const rx::ast::TypeRef &type, std::optional<TypeId> selfType){
    if(!selfType.has_value()) selfType = checker_.currentSelfType_;
    struct ContextGuard{
        std::optional<TypeId> &selfType;
        std::optional<TypeId> previousSelfType;
        ~ContextGuard(){ selfType = previousSelfType; }
    } context{checker_.currentSelfType_, checker_.currentSelfType_};
    checker_.currentSelfType_ = selfType;

    const TypeId errorType = checker_.model_.types.builtin(TypeKind::Error);
    TypeId result = errorType;
    if(dynamic_cast<const rx::ast::UnitTypeRef *>(&type) != nullptr){
        result = checker_.model_.types.builtin(TypeKind::Unit);
    }else if(const auto *path = dynamic_cast<const rx::ast::TypePathRef *>(&type)){
        return resolveTypePath(*path, selfType);
    }else if(const auto *reference = dynamic_cast<const rx::ast::ReferenceTypeRef *>(&type)){
        const TypeId referent = resolveType(reference->referent(), selfType);
        if(referent != errorType){
            result = checker_.model_.types.reference(referent, reference->isMutable());
        }
    }else if(const auto *array = dynamic_cast<const rx::ast::ArrayTypeRef *>(&type)){
        const TypeId elementType = resolveType(array->elementType(), selfType);
        const auto length = checker_.constEvaluate_.evaluateArrayLength(array->count());
        if(elementType != errorType && length.has_value()){
            result = checker_.model_.types.array(elementType, *length);
        }
    }else{
        checker_.diagnose(type, "unsupported type");
    }

    checker_.model_.resolvedTypes.insert_or_assign(type.id(), result);
    return result;
}

TypeId Resolution::resolveTypePath(const rx::ast::TypePathRef &type, std::optional<TypeId> selfType){
    if(!selfType.has_value()) selfType = checker_.currentSelfType_;
    const TypeId errorType = checker_.model_.types.builtin(TypeKind::Error);
    auto finish = [&](TypeId result){
        checker_.model_.resolvedTypes.insert_or_assign(type.id(), result);
        return result;
    };
    auto fail = [&](const rx::ast::ASTNode &node, std::string message){
        checker_.diagnose(node, std::move(message));
        return finish(errorType);
    };

    if(type.segments().size() != 1){
        return fail(type, "type path must contain exactly one segment");
    }
    const auto &segment = *type.segments().front();
    const std::string &name = segment.name();
    const std::size_t count = segment.hasGenericArgs() ? segment.genericArgs()->size() : 0;
    TypeId result = errorType;

    if(name == "Self"){
        if(count != 0) return fail(segment, "Self takes no concrete type arguments");
        if(!selfType.has_value()){
            return fail(segment, "Self is only allowed in a struct or impl context");
        }
        result = *selfType;
        if(checker_.model_.types.get(result).kind() == TypeKind::Struct){
            checker_.model_.resolvedSymbols.insert_or_assign(segment.id(), checker_.model_.types.get(result).symbol());
            checker_.model_.resolvedSymbols.insert_or_assign(type.id(), checker_.model_.types.get(result).symbol());
        }
        return finish(result);
    }

    const auto symbol = checker_.scope_.lookupType(name);
    if(!symbol.has_value()) return fail(segment, "unknown type '" + name + "'");
    const auto &info = checker_.model_.symbols.at(*symbol);
    if(info.kind == SymbolKind::Struct){
        if(count != 0) return fail(segment, "struct takes no concrete type arguments");
        result = checker_.model_.types.structure(*symbol);
    }else if(info.kind == SymbolKind::Builtin){
        static const std::unordered_map<std::string, TypeKind> primitives{
            {"i32", TypeKind::I32}, {"u32", TypeKind::U32},
            {"isize", TypeKind::Isize}, {"usize", TypeKind::Usize},
            {"bool", TypeKind::Bool}
        };
        const auto primitive = primitives.find(name);
        if(primitive != primitives.end()){
            if(segment.hasGenericArgs()){
                return fail(segment, "primitive type takes no generic arguments");
            }
            result = checker_.model_.types.builtin(primitive->second);
        }else if(name == "Box" || name == "Vec"){
            if(count != 1){
                return fail(segment, name + " requires exactly one concrete type argument");
            }
            const TypeId elementType = resolveType(*segment.genericArgs()->front(), selfType);
            if(elementType == errorType) return finish(errorType);
            result = name == "Box" ? checker_.model_.types.box(elementType) : checker_.model_.types.vec(elementType);
        }else{
            return fail(segment, "'" + name + "' is a trait, not a concrete type");
        }
    }else{
        return fail(segment, "'" + name + "' does not name a type");
    }

    checker_.model_.resolvedSymbols.insert_or_assign(segment.id(), *symbol);
    checker_.model_.resolvedSymbols.insert_or_assign(type.id(), *symbol);
    return finish(result);
}

std::optional<SymbolId> Resolution::resolveValuePath(const rx::ast::PathExpr &path){
    auto fail = [&](const rx::ast::ASTNode &node, std::string message) -> std::optional<SymbolId>{
        checker_.diagnose(node, std::move(message));
        return std::nullopt;
    };
    auto finish = [&](SymbolId symbol) -> std::optional<SymbolId>{
        const auto &segment = *path.segments().back();
        if(segment.hasGenericArgs()){
            if(!segment.genericArgs()->empty()){
                return fail(segment, "value path takes no concrete type arguments");
            }
            const auto kind = checker_.model_.symbols.at(symbol).kind;
            if(kind != SymbolKind::Function && kind != SymbolKind::Builtin){
                return fail(segment, "generic arguments require a function");
            }
        }
        checker_.model_.resolvedSymbols.insert_or_assign(segment.id(), symbol);
        checker_.model_.resolvedSymbols.insert_or_assign(path.id(), symbol);
        return symbol;
    };

    if(path.segments().empty() || path.segments().size() > 2){
        return fail(path, "unsupported value path");
    }
    if(path.segments().size() == 1){
        const auto &segment = *path.segments().front();
        const auto symbol = checker_.scope_.lookupValue(segment.name());
        if(!symbol.has_value()){
            return fail(segment, "unknown value '" + segment.name() + "'");
        }
        return finish(*symbol);
    }

    const auto &owner = *path.segments().front();
    const auto &member = *path.segments().back();
    const std::string &name = owner.name();
    const std::size_t count = owner.hasGenericArgs() ? owner.genericArgs()->size() : 0;
    const TypeId errorType = checker_.model_.types.builtin(TypeKind::Error);
    TypeId selfType = errorType;

    if(name == "Self"){
        if(count != 0) return fail(owner, "Self takes no concrete type arguments");
        if(!checker_.currentSelfType_.has_value()){
            return fail(owner, "Self is only allowed in a struct or impl context");
        }
        selfType = *checker_.currentSelfType_;
        if(selfType == errorType) return std::nullopt;
        checker_.model_.resolvedSymbols.insert_or_assign(owner.id(), checker_.model_.types.get(selfType).symbol());
    }else{
        const auto symbol = checker_.scope_.lookupType(name);
        if(!symbol.has_value()) return fail(owner, "unknown type '" + name + "'");
        const auto &info = checker_.model_.symbols.at(*symbol);
        if(info.kind == SymbolKind::Struct){
            if(count != 0) return fail(owner, "struct takes no concrete type arguments");
            selfType = checker_.model_.types.structure(*symbol);
        }else if(info.kind == SymbolKind::Builtin && (name == "Box" || name == "Vec")){
            if(count != 1){
                return fail(owner, name + " requires exactly one concrete type argument");
            }
            const TypeId elementType = resolveType(*owner.genericArgs()->front(), checker_.currentSelfType_);
            if(elementType == errorType) return std::nullopt;
            selfType = name == "Box" ? checker_.model_.types.box(elementType) : checker_.model_.types.vec(elementType);
        }else{
            static const std::unordered_map<std::string, TypeKind> primitives{
                {"i32", TypeKind::I32}, {"u32", TypeKind::U32},
                {"isize", TypeKind::Isize}, {"usize", TypeKind::Usize},
                {"bool", TypeKind::Bool}
            };
            const auto primitive = primitives.find(name);
            if(info.kind != SymbolKind::Builtin || primitive == primitives.end()){
                return fail(owner, "'" + name + "' is not a concrete type");
            }
            if(owner.hasGenericArgs()){
                return fail(owner, "primitive type takes no generic arguments");
            }
            selfType = checker_.model_.types.builtin(primitive->second);
        }
        checker_.model_.resolvedSymbols.insert_or_assign(owner.id(), *symbol);
    }
    // 为 collectCall 保留 Type::member 中 Type 的具体类型。
    checker_.model_.resolvedTypes.insert_or_assign(owner.id(), selfType);

    const auto &type = checker_.model_.types.get(selfType);
    bool builtin = false;
    if(type.kind() == TypeKind::Struct){
        const auto &structure = checker_.model_.declarations.structs.at(type.symbol());
        const auto symbol = structure.associatedValues.find(member.name());
        if(symbol != structure.associatedValues.end()) return finish(symbol->second);

        if(member.name() == "clone"){
            for(const auto &attribute : structure.declaration->attributes()){
                const auto *derive = dynamic_cast<const rx::ast::DeriveAttribute *>(attribute.get());
                if(derive != nullptr && std::find(derive->names().begin(), derive->names().end(), "Clone") != derive->names().end()){
                    builtin = true;
                    break;
                }
            }
        }
    }else{
        builtin = member.name() == "clone";
        if(type.kind() == TypeKind::Box || type.kind() == TypeKind::Vec){
            builtin = builtin || member.name() == "new";
        }
        if(type.kind() == TypeKind::Vec){
            builtin = builtin || member.name() == "len" || member.name() == "is_empty"
                      || member.name() == "push" || member.name() == "remove";
        }
    }
    if(!builtin) return fail(member, "unknown associated value '" + member.name() + "'");
    if(member.hasGenericArgs() && !member.genericArgs()->empty()){
        return fail(member, "builtin method takes no concrete type arguments");
    }

    // 同一具体类型的同名内建操作复用符号；不加入用户的全局作用域。
    const std::string key = "@" + std::to_string(selfType.value) + "::" + member.name();
    auto symbol = checker_.builtinSymbols_.find(key);
    if(symbol == checker_.builtinSymbols_.end()){
        const SymbolId allocated = checker_.allocateSymbol();
        checker_.model_.symbols.emplace(allocated, SymbolInfo{member.name(), SymbolKind::Builtin, 0, {}, false});
        symbol = checker_.builtinSymbols_.emplace(key, allocated).first;
    }
    return finish(symbol->second);
}
