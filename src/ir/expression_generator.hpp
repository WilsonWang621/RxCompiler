#pragma once

#include "generation_context.hpp"

class ExpressionGenerator{ //表达式分派、取值与取地址；使用已解析的符号和类型。
public:
    IRExprResult generateExpr(const rx::ast::Expr &expression, IRGenerationContext &context);
    // 地址只求一次；nullopt 表示地址计算过程中当前块已经终结。
    std::optional<IRPlace> generatePlace(const rx::ast::Expr &expression, IRGenerationContext &context);

private:
    std::optional<IRPlace> generateFieldPlace(const rx::ast::FieldExpr &expression, IRGenerationContext &context);
    IRExprResult generatePath(const rx::ast::PathExpr &expression, IRGenerationContext &context);
    IRExprResult generateBinary(const rx::ast::BinaryExpr &expression, IRGenerationContext &context);
    // 实参按源码顺序求值；方法调用还需按语义签名调整并传入 self。
    IRExprResult generateCall(const rx::ast::CallExpr &expression, IRGenerationContext &context);
    IRExprResult generateMethodCall(const rx::ast::MethodCallExpr &expression, IRGenerationContext &context);
    // 两种赋值均为：右侧 -> 目标地址 -> 读取/更新，目标地址只计算一次。
    IRExprResult generateAssignment(const rx::ast::AssignExpr &expression, IRGenerationContext &context);
    IRExprResult generateCompoundAssignment(const rx::ast::CompoundAssignExpr &expression, IRGenerationContext &context);
    IRExprResult generateReturn(const rx::ast::ReturnExpr &expression, IRGenerationContext &context);
};
