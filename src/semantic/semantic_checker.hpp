#pragma once

#include "semantic_model.hpp"
#include"ast.hpp"

#include<stdint.h>

class SemanticChecker{
private:
    SemanticModel model_;

    void firstScan(const rx::ast::Crate &crate);
    void secondScan(const rx::ast::Crate &crate);

    void collectFunction(const rx::ast::FunctionItem &function);
    void collectConstant(const rx::ast::ConstItem &constant);
    void collectStruct(const rx::ast::StructItem &structure);
    void collectImpl(const rx::ast::ImplItem &implementation);

public:
    SemanticModel check(const rx::ast::Crate &crate);
};