#pragma once

#include "ast.hpp"
#include "type.hpp"

#include <optional>

class SemanticChecker;

// 函数体、参数绑定和 impl 函数体。
class FunctionCollector{
private:
    SemanticChecker &checker_;

public:
    explicit FunctionCollector(SemanticChecker &checker) : checker_(checker){}

    void collectFunctionBody(const rx::ast::FunctionItem &function, std::optional<TypeId> selfType = std::nullopt);
    void collectFunctionParameters(const rx::ast::FunctionItem &function, std::optional<TypeId> selfType = std::nullopt);
    void collectParameter(const rx::ast::NamedFunctionParam &parameter);
    void collectSelfParameter(const rx::ast::SelfFunctionParam &parameter, TypeId selfType);
    void collectImplBodies(const rx::ast::ImplItem &implementation);
};
