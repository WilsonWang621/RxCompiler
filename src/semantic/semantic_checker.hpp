#pragma once

#include "semantic_model.hpp"
#include "ast.hpp"

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

class SemanticChecker{
private:
    SemanticModel model_;
    std::uint32_t nextSymbolId_ = 1;

    SymbolId allocateSymbol();
    void diagnose(const rx::ast::ASTNode &node, std::string message);

    void firstScan(const rx::ast::Crate &crate);    //第一次扫描收集信息和做部分检查
    void secondScan(const rx::ast::Crate &crate);   //第二次扫描完成剩余检查
    
    //封装第一次收集信息的函数
    void collectFunction(const rx::ast::FunctionItem &function);
    void collectConstant(const rx::ast::ConstItem &constant);
    void collectStruct(const rx::ast::StructItem &structure);
    void collectImpl(const rx::ast::ImplItem &implementation);

    // 第二次扫描先解析全部声明类型，再检查初始化式和函数体，支持前向调用。
    void prepareGlobalScope();
    void collectFunctionSignature(const rx::ast::FunctionItem &function, std::optional<TypeId> selfType = std::nullopt);
    void collectConstantType(const rx::ast::ConstItem &constant, std::optional<TypeId> selfType = std::nullopt);
    void collectStructField(const rx::ast::StructField &field, TypeId selfType);
    void collectImplTypes(const rx::ast::ImplItem &implementation);
    void collectFunctionBody(const rx::ast::FunctionItem &function, std::optional<TypeId> selfType = std::nullopt);
    void collectFunctionParameters(const rx::ast::FunctionItem &function, std::optional<TypeId> selfType = std::nullopt);
    void collectParameter(const rx::ast::NamedFunctionParam &parameter);
    void collectSelfParameter(const rx::ast::SelfFunctionParam &parameter, TypeId selfType);
    void collectImplBodies(const rx::ast::ImplItem &implementation);
    void checkMain(const rx::ast::Crate &crate);

    // TypeRef -> TypeId；selfType 只在当前结构体及其 impl 的类型上下文有效。
    TypeId resolveType(const rx::ast::TypeRef &type, std::optional<TypeId> selfType = std::nullopt);
    TypeId resolveTypePath(const rx::ast::TypePathRef &type, std::optional<TypeId> selfType = std::nullopt);
    std::optional<SymbolId> resolveValuePath(const rx::ast::PathExpr &path);

    // 常量按依赖求值并检测循环；长度求值失败用 nullopt，不能与合法长度 0 混淆。
    std::optional<ConstValue> evaluateConstant(SymbolId symbol);
    std::optional<ConstValue> evaluateConstantExpr(const rx::ast::Expr &expression, TypeId expectedType);
    std::optional<std::uint32_t> evaluateArrayLength(const rx::ast::Expr &expression);

    // 块入口管理作用域；函数体已声明参数后使用 collectBlockContents，避免重复入栈。
    TypeId collectFunctionBlock(const rx::ast::BlockExpr &body, std::optional<TypeId> expectedType = std::nullopt);
    TypeId collectBlockContents(const rx::ast::BlockExpr &body, std::optional<TypeId> expectedType = std::nullopt);
    TypeId collectStmt(const rx::ast::Stmt &stmt);
    void collectLetStmt(const rx::ast::LetStmt &stmt);             // let x : i32 = 20;
    TypeId collectExprStmt(const rx::ast::ExprStmt &stmt);

    // 统一表达式入口记录 expressionTypes / expressionInfo，并分发到具体节点。
    // expectedType 只按规范传递，不能无条件传进二元、一元或借用运算的操作数。
    TypeId collectExpr(const rx::ast::Expr &expression, std::optional<TypeId> expectedType = std::nullopt);
    TypeId collectUnit(const rx::ast::UnitExpr &expression);
    TypeId collectIntegerLiteral(const rx::ast::IntegerLiteralExpr &expression, std::optional<TypeId> expectedType = std::nullopt);
    TypeId collectBooleanLiteral(const rx::ast::BooleanLiteralExpr &expression);
    TypeId collectPath(const rx::ast::PathExpr &path);
    TypeId collectBinaryExpr(const rx::ast::BinaryExpr &expression); // b + c；赋值由 collectAssignment 处理。
    TypeId collectUnaryExpr(const rx::ast::UnaryExpr &expression);
    TypeId collectCast(const rx::ast::CastExpr &expression);
    TypeId collectAssignment(const rx::ast::AssignExpr &assignment);
    TypeId collectCompoundAssignment(const rx::ast::CompoundAssignExpr &assignment);
    TypeId collectArray(const rx::ast::ArrayExpr &array, std::optional<TypeId> expectedType = std::nullopt);
    TypeId collectRepeatedArray(const rx::ast::ArrayRepeatExpr &repeatedArray, std::optional<TypeId> expectedType = std::nullopt);
    TypeId collectIndex(const rx::ast::IndexExpr &expression);
    TypeId collectStructExpr(const rx::ast::StructExpr &expression);
    TypeId collectField(const rx::ast::FieldExpr &expression);
    TypeId collectCall(const rx::ast::CallExpr &expression);
    TypeId collectMethodCall(const rx::ast::MethodCallExpr &expression);
    TypeId collectIf(const rx::ast::IfExpr &ifstmt, std::optional<TypeId> expectedType = std::nullopt);
    TypeId collectLoop(const rx::ast::LoopExpr &loop, std::optional<TypeId> expectedType = std::nullopt);
    TypeId collectWhile(const rx::ast::WhileExpr &loop);
    // return 使用当前函数结果类型；break / continue 使用最近一层循环上下文。
    TypeId collectReturn(const rx::ast::ReturnExpr &returnstmt);
    TypeId collectBreak(const rx::ast::BreakExpr &breakstmt);
    TypeId collectContinue(const rx::ast::ContinueExpr &continuestmt);

    // 类型相等、隐式转换与 as 转换是不同规则；合并类型时保留所有先前结果。
    bool canCoerce(TypeId source, TypeId target) const;
    bool canCast(TypeId source, TypeId target) const;
    TypeId collectCommonType(const std::vector<TypeId> &types, const rx::ast::ASTNode &node);
    bool checkMutablePlace(const rx::ast::Expr &expression);
    bool hasCopy(TypeId type) const;
    bool hasClone(TypeId type) const;
    bool hasPartialEq(TypeId type) const;
    bool hasEq(TypeId type) const;
    void checkStructLayout(const rx::ast::StructItem &structure);
    void checkStructDerive(const rx::ast::StructItem &structure);
    

public:
    SemanticModel check(const rx::ast::Crate &crate);
};
