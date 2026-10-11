#pragma once

#include<ast.hpp>
#include <string>

enum class SymbolKind{
    Function,
    Constant,
    Struct,
    Parameter,
    Local,
    Receiver,
    Builtin
};

struct SymbolInfo{
    std::string name;
    SymbolKind kind;
    rx::ast::NodeId declaration;
    rx::ast::SourceSpan span;
    bool isMutable = false;
};