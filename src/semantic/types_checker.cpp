#include "types_checker.hpp"
#include "semantic_checker.hpp"

#include <algorithm>
#include <unordered_map>

bool TypesChecker::canCoerce(TypeId source, TypeId target) const{
    // 判断恒等、never、引用弱化以及规范允许的重复解引用再借用转换是否成立。
    const TypeId errorType = checker_.model_.types.builtin(TypeKind::Error);
    if(source == errorType || target == errorType) return false;
    if(source == target) return true;

    const Type *s = &checker_.model_.types.get(source);
    const Type *t = &checker_.model_.types.get(target);
    //Never is always OK!
    if(s->kind() == TypeKind::Never) return true;
    //both two sides should be reference
    if(s->kind() != TypeKind::Reference || t->kind() != TypeKind::Reference) return false;
    
    if(t->isMutable() && !s->isMutable()) return false;

    // 从源引用的所指类型开始，重复解引用；目标所指类型必须精确匹配。
    source = s->elementType();
    while(true){
        if(source == t->elementType()) return true;
        s = &checker_.model_.types.get(source);
        if(s->kind() == TypeKind::Reference){
            // 共享引用会阻断可变再借用，即使后面还有可变引用。
            if(t->isMutable() && !s->isMutable()) return false;
        }else if(s->kind() != TypeKind::Box){
            return false;
        }
        source = s->elementType();
    }
}

bool TypesChecker::canCast(TypeId source, TypeId target) const{
    // 判断源和目标是否属于 Rx 允许的整数互转或 bool 到整数显式转换。
    if(target != checker_.model_.types.builtin(TypeKind::I32) && target != checker_.model_.types.builtin(TypeKind::U32) && target != checker_.model_.types.builtin(TypeKind::Isize) && target != checker_.model_.types.builtin(TypeKind::Usize)){
        //checker_.model_.diagnostics.push_back(Diagnosis{});
        return false;
    }
    if(source != checker_.model_.types.builtin(TypeKind::I32) && source != checker_.model_.types.builtin(TypeKind::U32) && source != checker_.model_.types.builtin(TypeKind::Isize) && source != checker_.model_.types.builtin(TypeKind::Usize) || source != checker_.model_.types.builtin(TypeKind::Bool)){
        return false;
    }
    return true;
}

TypeId TypesChecker::collectCommonType(const std::vector<TypeId> &types, const rx::ast::ASTNode &node){
    // 忽略 never 后按源码顺序执行 LUB 转换，失败时诊断并返回 Error 类型。

}

bool TypesChecker::hasCopy(TypeId type) const{
    // 按内建类型、复合元素和结构体 derive 信息递归判断类型是否实现 Copy。
    std::unordered_map<TypeId, bool> results;
    return hasCopy(type, results);
}

bool TypesChecker::hasCopy(TypeId type, std::unordered_map<TypeId, bool> &results) const{
    const auto cached = results.find(type);
    if(cached != results.end()) return cached->second;

    // 先记录 false，使尚未完成的按值递归不会被误判为 Copy。
    results.emplace(type, false);
    const auto &info = checker_.model_.types.get(type);
    bool result = false;
    switch(info.kind()){
        case TypeKind::Unit: case TypeKind::Never:
        case TypeKind::Bool: case TypeKind::I32: case TypeKind::U32:
        case TypeKind::Isize: case TypeKind::Usize:
            result = true;
            break;
        case TypeKind::Reference:
            result = !info.isMutable();
            break;
        case TypeKind::Array:
            result = hasCopy(info.elementType(), results);
            break;
        case TypeKind::Struct:{
            const auto &structure = checker_.model_.declarations.structs.at(info.symbol());
            bool derivedCopy = false;
            bool derivedClone = false;
            for(const auto &attribute : structure.declaration->attributes()){
                const auto *derive = dynamic_cast<const rx::ast::DeriveAttribute *>(attribute.get());
                if(derive == nullptr) continue;
                for(const auto &name : derive->names()){
                    if(name == "Copy") derivedCopy = true;
                    if(name == "Clone") derivedClone = true;
                }
            }
            if(!derivedCopy || !derivedClone) break;

            result = true;
            for(const auto &[name, field] : structure.fields){
                const auto fieldType = structure.fieldTypes.find(name);
                if(fieldType == structure.fieldTypes.end() || !hasCopy(fieldType->second, results)){
                    result = false;
                    break;
                }
            }
            break;
        }
        case TypeKind::Error: case TypeKind::Box: case TypeKind::Vec:
            break;
    }
    results.at(type) = result;
    return result;
}

bool TypesChecker::hasClone(TypeId type) const{
    // 按内建类型、容器元素和结构体 derive 信息递归判断类型是否实现 Clone。
    std::unordered_map<TypeId, bool> results;
    return hasClone(type, results);
}

bool TypesChecker::hasClone(TypeId type, std::unordered_map<TypeId, bool> &results) const{
    const auto cached = results.find(type);
    if(cached != results.end()) return cached->second;

    // 允许递归类型闭环，其余字段仍须逐一满足 Clone。
    results.emplace(type, true);
    const auto &info = checker_.model_.types.get(type);
    bool result = false;
    switch(info.kind()){
        case TypeKind::Unit: case TypeKind::Never:
        case TypeKind::Bool: case TypeKind::I32: case TypeKind::U32:
        case TypeKind::Isize: case TypeKind::Usize:
            result = true;
            break;
        case TypeKind::Reference:
            result = !info.isMutable();
            break;
        case TypeKind::Array: case TypeKind::Box: case TypeKind::Vec:
            result = hasClone(info.elementType(), results);
            break;
        case TypeKind::Struct:{
            const auto &structure = checker_.model_.declarations.structs.at(info.symbol());
            bool derivedClone = false;
            for(const auto &attribute : structure.declaration->attributes()){
                const auto *derive = dynamic_cast<const rx::ast::DeriveAttribute *>(attribute.get());
                if(derive != nullptr && std::find(derive->names().begin(), derive->names().end(), "Clone") != derive->names().end()){
                    derivedClone = true;
                    break;
                }
            }
            if(!derivedClone) break;

            result = true;
            for(const auto &[name, field] : structure.fields){
                const auto fieldType = structure.fieldTypes.find(name);
                if(fieldType == structure.fieldTypes.end() || !hasClone(fieldType->second, results)){
                    result = false;
                    break;
                }
            }
            break;
        }
        case TypeKind::Error:
            break;
    }
    results.at(type) = result;
    return result;
}

bool TypesChecker::hasPartialEq(TypeId type) const{
    // 按标量、复合元素和结构体 derive 信息递归判断类型是否支持相等比较。
    std::unordered_map<TypeId, bool> results;
    return hasPartialEq(type, results);
}

bool TypesChecker::hasPartialEq(TypeId type, std::unordered_map<TypeId, bool> &results) const{
    const auto cached = results.find(type);
    if(cached != results.end()) return cached->second;

    // 递归闭环暂按成立处理，不跳过其他字段的能力检查。
    results.emplace(type, true);
    const auto &info = checker_.model_.types.get(type);
    bool result = false;
    switch(info.kind()){
        case TypeKind::Unit: case TypeKind::Never:
        case TypeKind::Bool: case TypeKind::I32: case TypeKind::U32:
        case TypeKind::Isize: case TypeKind::Usize:
            result = true;
            break;
        case TypeKind::Reference: case TypeKind::Array:
        case TypeKind::Box: case TypeKind::Vec:
            result = hasPartialEq(info.elementType(), results);
            break;
        case TypeKind::Struct:{
            const auto &structure = checker_.model_.declarations.structs.at(info.symbol());
            bool derivedPartialEq = false;
            for(const auto &attribute : structure.declaration->attributes()){
                const auto *derive = dynamic_cast<const rx::ast::DeriveAttribute *>(attribute.get());
                if(derive != nullptr && std::find(derive->names().begin(), derive->names().end(), "PartialEq") != derive->names().end()){
                    derivedPartialEq = true;
                    break;
                }
            }
            if(!derivedPartialEq) break;

            result = true;
            for(const auto &[name, field] : structure.fields){
                const auto fieldType = structure.fieldTypes.find(name);
                if(fieldType == structure.fieldTypes.end() || !hasPartialEq(fieldType->second, results)){
                    result = false;
                    break;
                }
            }
            break;
        }
        case TypeKind::Error:
            break;
    }
    results.at(type) = result;
    return result;
}

bool TypesChecker::hasEq(TypeId type) const{
    // 按标量、复合元素和结构体 derive 信息递归判断类型是否实现完整 Eq。
    std::unordered_map<TypeId, bool> results;
    return hasEq(type, results);
}

bool TypesChecker::hasEq(TypeId type, std::unordered_map<TypeId, bool> &results) const{
    const auto cached = results.find(type);
    if(cached != results.end()) return cached->second;

    // 递归闭环暂按成立处理，最终结果仍受所有字段约束。
    results.emplace(type, true);
    const auto &info = checker_.model_.types.get(type);
    bool result = false;
    switch(info.kind()){
        case TypeKind::Unit: case TypeKind::Never:
        case TypeKind::Bool: case TypeKind::I32: case TypeKind::U32:
        case TypeKind::Isize: case TypeKind::Usize:
            result = true;
            break;
        case TypeKind::Reference: case TypeKind::Array:
        case TypeKind::Box: case TypeKind::Vec:
            result = hasEq(info.elementType(), results);
            break;
        case TypeKind::Struct:{
            const auto &structure = checker_.model_.declarations.structs.at(info.symbol());
            bool derivedEq = false;
            bool derivedPartialEq = false;
            for(const auto &attribute : structure.declaration->attributes()){
                const auto *derive = dynamic_cast<const rx::ast::DeriveAttribute *>(attribute.get());
                if(derive == nullptr) continue;
                for(const auto &name : derive->names()){
                    if(name == "Eq") derivedEq = true;
                    if(name == "PartialEq") derivedPartialEq = true;
                }
            }
            if(!derivedEq || !derivedPartialEq) break;

            result = true;
            for(const auto &[name, field] : structure.fields){
                const auto fieldType = structure.fieldTypes.find(name);
                if(fieldType == structure.fieldTypes.end() || !hasEq(fieldType->second, results)){
                    result = false;
                    break;
                }
            }
            break;
        }
        case TypeKind::Error:
            break;
    }
    results.at(type) = result;
    return result;
}

void TypesChecker::checkStructLayout(const rx::ast::StructItem &structure){
    // 沿按值结构体和数组字段检测无限大小递归；引用、Box 和 Vec 截断递归路径。
    const auto symbol = checker_.model_.declaredSymbols.find(structure.id());
    if(symbol == checker_.model_.declaredSymbols.end()) return;

    std::unordered_map<TypeId, bool> results;
    const TypeId type = checker_.model_.types.structure(symbol->second);
    if(!checkStructLayout(type, results)){
        checker_.diagnose(structure, "struct '" + structure.name() + "' has an infinite-size layout");
    }
}

bool TypesChecker::checkStructLayout(TypeId type, std::unordered_map<TypeId, bool> &results) const{
    const auto cached = results.find(type);
    if(cached != results.end()) return cached->second;

    // 尚未完成的类型再次出现，说明存在按值包含环。
    results.emplace(type, false);
    const auto &info = checker_.model_.types.get(type);
    bool result = true;
    if(info.kind() == TypeKind::Array){
        result = checkStructLayout(info.elementType(), results);
    }else if(info.kind() == TypeKind::Struct){
        const auto &declaration = checker_.model_.declarations.structs.at(info.symbol());
        for(const auto &[name, field] : declaration.fields){
            const auto fieldType = declaration.fieldTypes.find(name);
            // 未解析或 Error 字段由类型解析器诊断，不追加无限大小诊断。
            if(fieldType == declaration.fieldTypes.end()) continue;
            if(!checkStructLayout(fieldType->second, results)){
                result = false;
                break;
            }
        }
    }
    results.at(type) = result;
    return result;
}

void TypesChecker::checkStructDerive(const rx::ast::StructItem &structure){
    // 校验 derive 名称、重复项和依赖关系，并确认所有字段满足派生 trait 的能力要求。
    const auto symbol = checker_.model_.declaredSymbols.find(structure.id());
    if(symbol == checker_.model_.declaredSymbols.end()) return;
    const auto &declaration = checker_.model_.declarations.structs.at(symbol->second);

    std::vector<std::string> derived;
    for(const auto &attribute : structure.attributes()){
        const auto *derive = dynamic_cast<const rx::ast::DeriveAttribute *>(attribute.get());
        if(derive == nullptr){
            checker_.diagnose(*attribute, "unsupported struct attribute");
            continue;
        }
        for(const auto &name : derive->names()){
            if(name != "Copy" && name != "Clone" && name != "PartialEq" && name != "Eq"){
                checker_.diagnose(*derive, "unsupported derive '" + name + "'");
            }else if(std::find(derived.begin(), derived.end(), name) != derived.end()){
                checker_.diagnose(*derive, "duplicate derive '" + name + "'");
            }else{
                derived.push_back(name);
            }
        }
    }

    if(std::find(derived.begin(), derived.end(), "Copy") != derived.end() &&
       std::find(derived.begin(), derived.end(), "Clone") == derived.end()){
        checker_.diagnose(structure, "deriving Copy requires explicitly deriving Clone");
    }
    if(std::find(derived.begin(), derived.end(), "Eq") != derived.end() &&
       std::find(derived.begin(), derived.end(), "PartialEq") == derived.end()){
        checker_.diagnose(structure, "deriving Eq requires explicitly deriving PartialEq");
    }

    for(const auto &field : structure.fields()){
        const auto retained = declaration.fields.find(field->name());
        if(retained == declaration.fields.end() || retained->second != field.get()) continue;
        const auto fieldType = declaration.fieldTypes.find(field->name());
        if(fieldType == declaration.fieldTypes.end() || fieldType->second == checker_.model_.types.builtin(TypeKind::Error)) continue;

        for(const auto &name : derived){
            bool supported = false;
            if(name == "Copy") supported = hasCopy(fieldType->second);
            else if(name == "Clone") supported = hasClone(fieldType->second);
            else if(name == "PartialEq") supported = hasPartialEq(fieldType->second);
            else if(name == "Eq") supported = hasEq(fieldType->second);
            if(!supported){
                checker_.diagnose(*field, "field '" + field->name() + "' does not implement '" + name + "' required by derive");
            }
        }
    }
}
