#pragma once

#include "module.hpp"

class IRBuilder{ //只构造 IR，不进行 AST 遍历或名字解析。
private:
    IRModule *module_ = nullptr;
    IRFunction *function_ = nullptr;
    IRBasicBlock *block_ = nullptr;
    unsigned nextValueId_ = 0;
    bool ownsFunction(const IRFunction &function) const;   //判断 function 是否属于当前 Builder的IRModule
    void append(IRInstruction instruction);

public:
    void reset(IRModule *module = nullptr);

    IRStruct &declareStruct(std::string name, std::vector<IRType> fields);
    IRFunction &declareFunction(std::string name, IRType returnType, std::vector<IRParameter> parameters, bool isInternal = true);
    void beginFunction(IRFunction &function); // 仅开始已登记的声明一次；清空插入位置并重置临时编号。
    // 只创建块；插入位置由 setInsertPoint 显式设置。
    IRBasicBlock &createBlock(IRFunction &function, std::string name);
    void setInsertPoint(IRBasicBlock &block); // 当前函数内切换，不重置编号。
    bool isTerminated() const; // 查询当前块；未设置插入位置时抛出异常。

    IRValue createAlloca(IRType type); // I1/I32/Ptr 的栈槽，返回 Ptr。
    IRValue createLoad(IRType type, IRValue address); // opaque ptr 需要显式给出加载类型。
    void createStore(IRValue value, IRValue address); // 按 value.type 写入。
    IRValue createBinary(IROpcode opcode, IRValue left, IRValue right); // Add/Sub，结果为 I32。
    IRValue createCompare(IRComparePredicate predicate, IRValue left, IRValue right); // 结果为 I1。
    IRValue createFieldAddress(const IRStruct &structure, IRValue address, std::size_t fieldIndex);
    // arguments 包含 self；无返回值为 nullopt，否则创建并返回一个 SSA 结果。
    std::optional<IRValue> createCall(const IRFunction &callee, std::vector<IRValue> arguments);
    void createReturn(std::optional<IRValue> value = std::nullopt); // 与当前返回类型匹配。
};
