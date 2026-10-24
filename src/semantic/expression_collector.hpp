#pragma once

#include "ast.hpp"
#include "type.hpp"

#include <optional>

class SemanticChecker;

// 普通表达式、调用和可变位置检查。
class ExpressionCollector{
private:
    SemanticChecker &checker_;
    bool hasSharedAccess(const rx::ast::Expr &expression) const;
    bool collectBuiltinSignature(TypeId owner, const std::string &name, std::vector<TypeId> &parameters, TypeId &result);
    TypeId checkCallArguments(const std::vector<rx::ast::ExprPtr> &arguments, const std::vector<TypeId> &parameters, TypeId result, const rx::ast::ASTNode &node);

public:
    explicit ExpressionCollector(SemanticChecker &checker) : checker_(checker){}

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
    bool checkMutablePlace(const rx::ast::Expr &expression);
};
