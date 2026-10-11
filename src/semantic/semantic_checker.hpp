#pragma once

#include "semantic_model.hpp"
#include "ast.hpp"

#include <cstdint>
#include <string>

class SemanticChecker{
private:
    SemanticModel model_;
    std::uint32_t nextSymbolId_ = 1;

    SymbolId allocateSymbol();
    void diagnose(const rx::ast::ASTNode &node, std::string message);

    void firstScan(const rx::ast::Crate &crate);
    void secondScan(const rx::ast::Crate &crate);

    void collectFunction(const rx::ast::FunctionItem &function);
    void collectConstant(const rx::ast::ConstItem &constant);
    void collectStruct(const rx::ast::StructItem &structure);
    void collectImpl(const rx::ast::ImplItem &implementation);

public:
    SemanticModel check(const rx::ast::Crate &crate);
};
