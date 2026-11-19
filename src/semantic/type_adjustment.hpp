#pragma once

#include "type.hpp"

#include <vector>

enum class DereferenceKind{Reference, Box};
enum class BorrowKind{None, Shared, Mutable};
enum class CoercionKind{Identity, Reference, Never};

struct DereferenceStep{
    DereferenceKind kind = DereferenceKind::Reference;
    TypeId sourceType; // 对应的 Reference 或 Box 类型。
    TypeId targetType; // 必须是sourceType 的 elementType。
};

struct CoercionInput{
    rx::ast::NodeId expression = 0; // 0 只表示 bare break 等隐式 unit，无需记录节点转换。
    TypeId sourceType; // 保存本次合流前的原始结果类型，不能用中途的 LUB 目标覆盖。
};

struct CoercionPlan{
    CoercionKind kind = CoercionKind::Identity;
    TypeId sourceType;
    TypeId targetType;
    // Reference 计划从 sourceType.elementType 开始，不包含最外层引用的解引用。
    // IR 输入值本身已是该目标的地址；每一步再读取一层引用或 Box 指针。
    std::vector<DereferenceStep> dereferences;
    BorrowKind borrow = BorrowKind::None; // Reference 计划按目标引用选择 Shared / Mutable。
    // &mut T -> &mut T 的再借用也记录为 Reference，可没有额外解引用步骤。
    // Identity 不调整地址；Never 只接受已终结的 IR 结果，不制造运行时值。
};

struct ReceiverAdjustment{
    TypeId sourceType; // 接收者表达式自身的类型。
    TypeId targetType; // 最终 self 参数类型。
    // 从保存 sourceType 的 place 开始，按方法查找选定的路径依次解引用。
    std::vector<DereferenceStep> dereferences;
    BorrowKind borrow = BorrowKind::None; // None 读取最终 place 的值，否则取其地址。
    bool reborrow = false; // 直接匹配已有 &mut 接收者时再借用；此时 borrow 为 None。
};
