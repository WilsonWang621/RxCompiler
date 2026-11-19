#pragma once

#include "generation_context.hpp"
#include <functional>

class IRGenerator{ //遍历 AST 并直接使用 context_.builder 构造 IR；实现按职责分文件。
private:
    IRGenerationContext context_;

    uint32_t basicBlockId = 0; //防止基本快命名重名

    //先第一遍收集声明
    void generateItem(const rx::ast::Item &item);

    // declaration_generator.cpp：第一遍登记类型、常量和完整函数签名，同semantic
    void declareItem(const rx::ast::Item &item);
    void declareBuiltins();
    void predeclareStructs(const rx::ast::StructItem &item);
    void declareStruct(const rx::ast::StructItem &item);
    void declareConstant(const rx::ast::ConstItem &item);
    void declareFunction(const rx::ast::FunctionItem &function);
    std::string mangleSymbol(SymbolId symbol, const SemanticModel &model) const;

    // function generator
    void bindParameters(const rx::ast::FunctionItem &function); // self 在首位；为入参建栈槽，mut 参数修改栈槽，不改写 SSA 入参。
    void generateFunction(const rx::ast::FunctionItem &function);  // 进入函数清空 locals；块已终结则结束，否则返回尾值或 unit。
    void generateMainWrapper();

    // statement_generator
    void generateLet(const rx::ast::LetStmt &stmt);
    IRExprResult generateStmt(const rx::ast::Stmt &stmt);
    IRExprResult generateBlock(const rx::ast::BlockExpr &block); // 语句依次执行；终结后停止；尾表达式的值交给 generateExpr。

    // array_generator.cpp：数组构造与索引
    IRExprResult generateArray(const rx::ast::ArrayExpr &expression);
    IRExprResult generateRepeatedArray(const rx::ast::ArrayRepeatExpr &expression);
    std::optional<IRPlace> generateIndexPlace(const rx::ast::IndexExpr &expression);

    IRExprResult generateStruct(const rx::ast::StructExpr &expression);

    // expression_generator
    IRExprResult generateExpr(const rx::ast::Expr &expression);
    IRValue materializeValue(const IRExprResult &result, TypeId type) const; // 存储或传参时，将无值的 unit 结果表示为 {}。
    // 统一执行语义阶段记录的结果转换。
    // 只求值一次，再应用该结果位置的模型计划；unit / terminated 仍沿用 IRExprResult。
    IRExprResult generateCoercedExpr(const rx::ast::Expr &expression);
    IRExprResult applyCoercion(const IRExprResult &result, const CoercionPlan &plan);
    // place 直接使用；临时值保存到入口栈槽。用于字段、索引和接收者的地址需求。
    std::optional<IRPlace> generatePlaceOrTemporary(const rx::ast::Expr &expression);
    IRPlace applyDereferences(IRPlace place, const std::vector<DereferenceStep> &steps);
    std::optional<IRPlace> generatePlace(const rx::ast::Expr &expression);  //找到存储地址
    std::optional<IRPlace> generateFieldPlace(const rx::ast::FieldExpr &expression); //找到 s.x 的地址
    IRExprResult generatePath(const rx::ast::PathExpr &expression);
    IRExprResult generateBinary(const rx::ast::BinaryExpr &expression);
    // 实参按源码顺序求值；方法调用还需按语义签名调整并传入 self。
    IRExprResult generateCall(const rx::ast::CallExpr &expression);
    IRExprResult generateMethodCall(const rx::ast::MethodCallExpr &expression);
    // 先生成一次接收者并执行选定调整，再依次生成普通实参；self 放在首位。
    IRExprResult generateReceiver(const rx::ast::Expr &expression, const ReceiverAdjustment &adjustment);
    // arguments 已全部求值、转换并物化，顺序与完整 parameterTypes 一致。
    // 普通目标经 context_.functions 发出调用，内建目标按操作种类和 ownerType 分派。
    IRExprResult emitResolvedCall(const ResolvedCall &call, std::vector<IRValue> arguments);
    IRExprResult emitBuiltinCall(const ResolvedCall &call, std::vector<IRValue> arguments);
    IRExprResult generateEquality(const rx::ast::BinaryExpr &expression);
    IRExprResult emitContainerCall(const ResolvedCall &call, const std::vector<IRValue> &arguments);
    IRValue allocateElements(IRType element, IRValue count);
    IRValue elementByteCount(IRType element, IRValue count);
    void copyElements(IRType element, IRValue destination, IRValue source, IRValue count, bool mayOverlap);
    IRValue elementAddress(IRType element, IRValue base, IRValue index);
    IRValue vectorField(IRValue address, std::size_t index);
    IRValue vectorElementAddress(IRType element, IRValue vector, IRValue index);
    IRValue emptyVector();
    void forEachElement(IRValue count, const std::function<void(IRValue)> &body);
    IRFunction &valueHelper(TypeId type, bool clone);
    void generateValueHelpers();
    void generateCloneBody(TypeId type, IRValue source);
    void generateEqualityBody(TypeId type, IRValue left, IRValue right);
    // 两种赋值均为：右侧 -> 目标地址 -> 读取/更新，目标地址只计算一次。
    IRExprResult generateAssignment(const rx::ast::AssignExpr &expression);
    IRExprResult generateCompoundAssignment(const rx::ast::CompoundAssignExpr &expression);
    IRExprResult generateUnary(const rx::ast::UnaryExpr &expression);
    IRExprResult generateCast(const rx::ast::CastExpr &expression);

    // control_flow_generator.cpp：控制流表达式。
    IRExprResult generateReturn(const rx::ast::ReturnExpr &expression);
    IRExprResult generateLogical(const rx::ast::BinaryExpr &expression);
    IRExprResult generateIf(const rx::ast::IfExpr &expression);
    IRExprResult generateContinue(const rx::ast::ContinueExpr &expression);
    IRExprResult generateBreak(const rx::ast::BreakExpr &expression);
    IRExprResult generateLoop(const rx::ast::LoopExpr &expression);
    IRExprResult generateWhile(const rx::ast::WhileExpr &expression);

public:
    IRGenerator() = default;
    IRGenerator(const IRGenerator &) = delete;
    IRGenerator &operator=(const IRGenerator &) = delete;
    IRGenerator(IRGenerator &&) = delete;
    IRGenerator &operator=(IRGenerator &&) = delete;

    // reset -> 声明扫描 -> 函数体扫描 -> 包装器 -> clear。
    // 具体生成步骤尚未实现时抛出异常；成功或异常退出均清理上下文。
    IRModule generate(const rx::ast::Crate &crate, const SemanticModel &model);
};
