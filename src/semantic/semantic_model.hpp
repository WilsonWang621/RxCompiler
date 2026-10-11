#pragma once

#include <ast.hpp>
#include "symbol.hpp"

#include <unordered_map>

struct TypeId   { uint32_t value; };


struct ExprInfo{
    bool mut;
    
};

struct ConstValue{

};

struct Diagnosis{
    rx::ast::SourceSpan span;
    std::string message;
};

struct SemanticModel{
    std::unordered_map<SymbolId, SymbolInfo> symbols;

    std::unordered_map<rx::ast::NodeId, TypeId> expression_types;
    std::unordered_map<rx::ast::NodeId, SymbolId> resolved_symbols;
    std::unordered_map<rx::ast::NodeId, ConstValue> constant_values;
};
