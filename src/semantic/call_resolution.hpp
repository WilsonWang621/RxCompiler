#pragma once

#include "symbol.hpp"
#include "type_adjustment.hpp"

#include <optional>
#include <vector>

enum class BuiltinOperation{
    None, GetI32, PrintI32, PrintlnI32, Clone, ArrayLen,
    BoxNew, VecNew, VecLen, VecIsEmpty, VecPush, VecRemove
};

struct CallTarget{
    SymbolId symbol; // 保留语义阶段分配的目标符号。
    BuiltinOperation builtin = BuiltinOperation::None; // None 表示普通函数或固有方法。
    TypeId ownerType; // 关联项 / 内建操作所属的完整具体类型；普通自由函数及 I/O 为 0。
};

struct ResolvedCall{
    CallTarget target;
    std::vector<TypeId> parameterTypes; // 完整签名；有 self 时位于首位，unit 参数也保留。
    TypeId returnType;
    // 仅点调用具有接收者调整，targetType 必须等于 parameterTypes.front()。
    // Type::method(receiver, ...) 没有该项，其 self 是第一个普通实参。
    std::optional<ReceiverAdjustment> receiver;
};
