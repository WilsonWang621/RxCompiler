#pragma once

#include "ir.hpp"

#include <algorithm>

struct IRStorageLayout{
    std::uint64_t size;
    std::uint32_t alignment;
};

inline IRStorageLayout storageLayout(const IRType &type){
    const auto alignUp = [](std::uint64_t size, std::uint32_t alignment){
        return (size + alignment - 1) / alignment * alignment;
    };
    switch(type.kind()){
        case IRType::Unit: return {0, 1};
        case IRType::I1: return {1, 1};
        case IRType::I32: case IRType::Ptr: return {4, 4};
        case IRType::Array:{
            const auto element = storageLayout(type.elementType());
            return {element.size * type.length(), element.alignment};
        }
        case IRType::Struct:{
            IRStorageLayout result{0, 1};
            for(const auto &field : type.structure()->fields){
                const auto layout = storageLayout(field);
                result.size = alignUp(result.size, layout.alignment) + layout.size;
                result.alignment = std::max(result.alignment, layout.alignment);
            }
            result.size = alignUp(result.size, result.alignment);
            return result;
        }
        case IRType::Void: throw std::logic_error("void has no storage layout");
    }
    throw std::logic_error("unknown IR storage type");
}
