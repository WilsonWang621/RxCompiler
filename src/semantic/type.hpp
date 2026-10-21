#pragma once

#include "symbol.hpp"

#include <cstddef>
#include <cstdint>
#include <deque>
#include <functional>
#include <initializer_list>
#include <limits>
#include <stdexcept>
#include <unordered_map>

struct TypeId{
    std::uint32_t value = 0; // 0 表示尚未解析，不代表错误类型。

    bool operator==(TypeId other) const { return value == other.value; }
    bool operator!=(TypeId other) const { return !(*this == other); }
};

namespace std{

template<>
struct hash<TypeId>{
    std::size_t operator()(TypeId id) const noexcept {
        return std::hash<std::uint32_t>{}(id.value);
    }
};

}

enum class TypeKind{
    Error, // 语义错误恢复时使用，避免把错误误认为 unit 或 never。
    Unit,
    Never,
    Bool,
    I32,
    U32,
    Isize,
    Usize,
    Struct,
    Reference,
    Array,
    Box,
    Vec
};

// 只保存解析后的类型，不保存 AST TypeRef、生命周期或 LLVM 类型
// 非当前 kind 使用的字段保持默认值；只能由 TypeTable 构造
class Type{
    friend class TypeTable;

    TypeKind kind_;
    TypeId elementType_;
    SymbolId symbol_;
    std::uint32_t length_ = 0; // Rx usize 为 32 位；数组长度已经过常量求值。
    bool isMutable_ = false; // 仅用于引用，与变量绑定本身的 mut 无关。

    explicit Type(TypeKind kind, TypeId elementType = {}, SymbolId symbol = {}, std::uint32_t length = 0, bool isMutable = false)
        : kind_(kind), elementType_(elementType), symbol_(symbol), length_(length), isMutable_(isMutable) {}

public:
    TypeKind kind() const { return kind_; }
    TypeId elementType() const { return elementType_; }
    SymbolId symbol() const { return symbol_; }
    std::uint32_t length() const { return length_; }
    bool isMutable() const { return isMutable_; }

    bool isInteger() const {
        return kind_ == TypeKind::I32 || kind_ == TypeKind::U32 || kind_ == TypeKind::Isize || kind_ == TypeKind::Usize;
    }

    bool isSignedInteger() const {
        return kind_ == TypeKind::I32 || kind_ == TypeKind::Isize;
    }

    bool operator==(const Type &other) const {
        return kind_ == other.kind_ && elementType_ == other.elementType_ && symbol_ == other.symbol_ && length_ == other.length_ && isMutable_ == other.isMutable_;
    }
};

// 同一张表内，相同类型得到相同 TypeId；ID 不可跨 SemanticModel 使用。
// 类型不可修改，deque 保证新增类型不会使已取得的 Type 引用失效。
class TypeTable{
    struct TypeHash{
        std::size_t operator()(const Type &type) const noexcept {
            std::size_t result = static_cast<std::size_t>(type.kind());
            for(std::size_t value : {static_cast<std::size_t>(type.elementType().value),
                                    static_cast<std::size_t>(type.symbol().value),
                                    static_cast<std::size_t>(type.length()),
                                    static_cast<std::size_t>(type.isMutable())}){
                result ^= value + 0x9e3779b9u + (result << 6) + (result >> 2);
            }
            return result;
        }
    };

    std::deque<Type> types_;  //TypeId ──get()──> Type
    std::unordered_map<Type, TypeId, TypeHash> ids_;  //Type   ──intern()──> TypeId

    TypeId getOrCreate(const Type &type){
        //已经存在就直接复用 ID。不存在则分配新 ID
        const auto found = ids_.find(type);
        if(found != ids_.end()) return found->second;
        if(types_.size() >= std::numeric_limits<std::uint32_t>::max()){
            throw std::overflow_error("too many semantic types");
        }

        const TypeId id{static_cast<std::uint32_t>(types_.size() + 1)};
        types_.push_back(type);
        try{
            ids_.emplace(type, id);
        }catch(...){
            types_.pop_back();
            throw;
        }
        return id;
    }

public:
    TypeTable(){
        for(TypeKind kind : {TypeKind::Error, TypeKind::Unit, TypeKind::Never,
                             TypeKind::Bool, TypeKind::I32, TypeKind::U32,
                             TypeKind::Isize, TypeKind::Usize}){
            getOrCreate(Type{kind});
        }
    }

    TypeId builtin(TypeKind kind) const {
        switch(kind){
            case TypeKind::Error: case TypeKind::Unit: case TypeKind::Never:
            case TypeKind::Bool: case TypeKind::I32: case TypeKind::U32:
            case TypeKind::Isize: case TypeKind::Usize:
                return TypeId{static_cast<std::uint32_t>(kind) + 1};
            default:
                throw std::invalid_argument("type requires additional information");
        }
    }

    const Type &get(TypeId id) const {
        if(id.value == 0 || id.value > types_.size()){
            throw std::out_of_range("invalid semantic type id");
        }
        return types_[id.value - 1];
    }

    TypeId structure(SymbolId symbol){
        if(symbol.value == 0) throw std::invalid_argument("invalid struct symbol");
        return getOrCreate(Type{TypeKind::Struct, {}, symbol});
    }

    TypeId reference(TypeId elementType, bool isMutable = false){
        get(elementType);
        return getOrCreate(Type{TypeKind::Reference, elementType, {}, 0, isMutable});
    }

    TypeId array(TypeId elementType, std::uint32_t length){
        get(elementType);
        return getOrCreate(Type{TypeKind::Array, elementType, {}, length});
    }

    TypeId box(TypeId elementType){
        get(elementType);
        return getOrCreate(Type{TypeKind::Box, elementType});
    }

    TypeId vec(TypeId elementType){
        get(elementType);
        return getOrCreate(Type{TypeKind::Vec, elementType});
    }
};
