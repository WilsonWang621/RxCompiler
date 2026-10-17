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

    void firstScan(const rx::ast::Crate &crate);    //第一次扫描收集信息和做部分检查
    void secondScan(const rx::ast::Crate &crate);   //第二次扫描完成剩余检查
    
    //封装收集信息的函数
    void collectFunction(const rx::ast::FunctionItem &function);
    void collectConstant(const rx::ast::ConstItem &constant);
    void collectStruct(const rx::ast::StructItem &structure);
    void collectImpl(const rx::ast::ImplItem &implementation);

public:
    SemanticModel check(const rx::ast::Crate &crate);
};
