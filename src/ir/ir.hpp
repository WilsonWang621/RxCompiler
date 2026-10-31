#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>

enum class IRType{ Void, I32, Ptr, I1 };
enum class IROpcode{ Alloca, Store, Call, Ret, Load, Add, Sub, ICmp, GetElementPtr };
enum class IRComparePredicate{ Eq, Ne, Sgt, Sge, Slt, Sle, Ugt, Uge, Ult, Ule };

struct IRValue{
    IRType type = IRType::I32;
    std::string name; // 非空表示局部 IR 值，不带 %；为空时使用 bits。
    std::uint32_t bits = 0; // 整数保存补码位模式，I1 只能是 0 或 1。
};

struct IRParameter{
    IRType type = IRType::I32;
    std::string name; 
};

struct IRStruct{
    std::string name; // LLVM 命名结构体的名称，不带 %。
    std::vector<IRType> fields; // 本轮覆盖标量和引用字段，顺序与 AST 声明一致。
};

struct IRInstruction{
    IROpcode opcode = IROpcode::Ret;
    std::optional<IRValue> result; // 非 void 调用始终有结果，调用方可丢弃该值。
    // load: 地址；store: 值、地址；add/sub/icmp: 左、右操作数；
    // GEP: 基地址；call: 完整实参列表；ret: 空或返回值；alloca: 空。
    std::vector<IRValue> operands;
    std::string callee; // 仅 call 使用，不带 @；返回类型由 result 决定。
    IRType allocatedType = IRType::I32; // 仅 alloca 使用，结果类型为 Ptr。
    IRComparePredicate predicate = IRComparePredicate::Eq; // 仅 ICmp 使用。
    const IRStruct *structure = nullptr; // 仅结构体 GEP 使用，由 Module 拥有。
    std::size_t fieldIndex = 0; // 按 structure->fields 索引。
};

struct IRBasicBlock{
    std::string name;
    std::vector<IRInstruction> instructions;
};

struct IRFunction{
    std::string name;
    IRType returnType = IRType::Void;
    bool isInternal = false;
    std::vector<std::unique_ptr<IRBasicBlock>> blocks; // 首块为入口。
    std::vector<IRParameter> parameters; // self 存在时位于第一个参数。
    bool isDeclaration = false; // 声明没有块；beginFunction 开始生成定义。
    bool isBuiltin = false; // 内置 I/O 声明在输出时附带运行时定义。
};
