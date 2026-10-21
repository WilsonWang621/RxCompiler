#pragma once

#include "ast.hpp"
#include "type.hpp"

#include <optional>

class SemanticChecker;

// 全局声明、函数签名和 impl 类型。
class DeclarationCollector{
private:
    SemanticChecker &checker_;

public:
    explicit DeclarationCollector(SemanticChecker &checker) : checker_(checker){}

    // 第二次扫描先解析全部声明类型，再检查初始化式和函数体，支持前向调用。
    void prepareGlobalScope();
    void collectFunction(const rx::ast::FunctionItem &function);
    void collectConstant(const rx::ast::ConstItem &constant);
    void collectStruct(const rx::ast::StructItem &structure);
    void collectImpl(const rx::ast::ImplItem &implementation);
    void collectFunctionSignature(const rx::ast::FunctionItem &function, std::optional<TypeId> selfType = std::nullopt);
    void collectConstantType(const rx::ast::ConstItem &constant, std::optional<TypeId> selfType = std::nullopt);
    void collectStructField(const rx::ast::StructField &field, TypeId selfType);
    void collectImplTypes(const rx::ast::ImplItem &implementation);
};
