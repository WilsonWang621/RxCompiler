#pragma once

#include<ast.hpp>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>

struct SymbolId{
    std::uint32_t value = 0; // 0 表示无效符号；ID 由语义模型分配。

    bool operator==(SymbolId other) const { return value == other.value; }
    bool operator!=(SymbolId other) const { return !(*this == other); }
};

namespace std{

template<>
struct hash<SymbolId>{
    std::size_t operator()(SymbolId id) const noexcept {
        return std::hash<std::uint32_t>{}(id.value);
    }
};

}

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
