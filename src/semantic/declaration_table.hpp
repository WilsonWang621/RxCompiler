//给“声明”建立快速查找索
#pragma once

#include "symbol.hpp"
#include "type.hpp"

#include <ast.hpp>

#include <string>
#include <optional>
#include <unordered_map>
#include <vector>

// 第一次语义扫描只收集声明，不检查函数体和常量表达式。
// 下列指针均不拥有 AST；DeclarationTable 的生命周期不能超过对应 Crate。
struct FunctionDeclarationInfo{
    SymbolId symbol;
    const rx::ast::FunctionItem *declaration = nullptr;
    // 普通参数名到源码顺序的映射；self 不在此表中。
    std::unordered_map<std::string, std::size_t> parameters;
    // 普通参数类型按源码顺序保存，self 单独保存；TypeId{0} 表示尚未解析。
    std::vector<TypeId> parameterTypes;
    std::optional<TypeId> receiverType;
    TypeId returnType;

    FunctionDeclarationInfo(SymbolId symbol, const rx::ast::FunctionItem *declaration): symbol(symbol), declaration(declaration) {}
};

struct ConstantDeclarationInfo{
    SymbolId symbol;
    const rx::ast::ConstItem *declaration = nullptr;
    TypeId type;

    ConstantDeclarationInfo(SymbolId symbol, const rx::ast::ConstItem *declaration): symbol(symbol), declaration(declaration) {}
};

struct StructDeclarationInfo{
    SymbolId symbol;
    const rx::ast::StructItem *declaration = nullptr;
    std::unordered_map<std::string, const rx::ast::StructField *> fields;
    std::unordered_map<std::string, TypeId> fieldTypes;

    // 同一个结构体可以有多个 impl，关联函数和关联常量共享此命名空间。
    std::unordered_map<std::string, SymbolId> associatedValues;
    std::vector<const rx::ast::ImplItem *> implBlocks;

    StructDeclarationInfo(SymbolId symbol, const rx::ast::StructItem *declaration): symbol(symbol), declaration(declaration) {}
};

struct DeclarationTable{
    // 顶层函数和常量共享值命名空间，结构体位于类型命名空间。
    // 分开保存避免未来增加局部作用域时把两类查找混在一起。
    std::unordered_map<std::string, SymbolId> globalValues;
    std::unordered_map<std::string, SymbolId> globalTypes;

    // 详细信息以稳定的 SymbolId 为键；名字解析后无需再次依赖字符串。
    std::unordered_map<SymbolId, FunctionDeclarationInfo> functions;
    std::unordered_map<SymbolId, ConstantDeclarationInfo> constants;
    std::unordered_map<SymbolId, StructDeclarationInfo> structs;

    void clear(){
        globalValues.clear();
        globalTypes.clear();
        functions.clear();
        constants.clear();
        structs.clear();
    }
};
