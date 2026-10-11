#pragma once

#include <cstdint>

namespace rx::ast{

using NodeId = std::uint32_t;

inline NodeId allocateNodeId(){
    static NodeId nextNodeId = 1; // 0表示无效ID
    return nextNodeId++;
}

}