#pragma once

#include"ParserBaseVisitor.h"
#include"ast.hpp"

namespace rx::frontend{

class ASTBuilder final : public ParserBaseVisitor{

    // 各 build 入口适配不同的 Context；优先级由语法决定，公共构建逻辑在 cpp 中复用。
    std::unique_ptr<ast::Item> buildItem(rx::Parser::ItemContext *ctx);

    std::unique_ptr<ast::FunctionItem> buildFunction(rx::Parser::FunctionDefinitionContext *ctx);

    std::unique_ptr<ast::ConstItem> buildConstItem(rx::Parser::ConstantItemContext *ctx);

    std::unique_ptr<ast::BlockExpr> buildBlock(rx::Parser::BlockExpressionContext *ctx);

    //1.1
    ast::StmtPtr buildStatement(rx::Parser::StatementContext *ctx);

    std::unique_ptr<ast::LetStmt> buildLet(rx::Parser::LetStatementContext *ctx);

    ast::ExprPtr buildExpression(rx::Parser::ExpressionContext *ctx);

    ast::ExprPtr buildLiteral(rx::Parser::LiteralExpressionContext *ctx);

    // 根据分号区分 [a, b, ...] 和 [value; count]，共用一个数组入口。
    ast::ExprPtr buildArray(rx::Parser::ArrayExpressionContext *ctx);

    // 保存常量初始化值或数组重复次数的表达式，具体数值留给语义分析求出。
    ast::ExprPtr buildConstValue(rx::Parser::ConstValueContext *ctx);

    // constValue 中负号后的字面量、路径或括号内容。
    ast::ExprPtr buildMagnitude(rx::Parser::MagnitudeContext *ctx);

    //1.2
    ast::ExprPtr buildAdditive(rx::Parser::AdditiveExpressionContext *ctx);

    ast::ExprPtr buildMultiplicative(rx::Parser::MultiplicativeExpressionContext *ctx);

    ast::ExprPtr buildCast(rx::Parser::CastExpressionContext *ctx);

    ast::ExprPtr buildUnary(rx::Parser::UnaryExpressionContext *ctx);

    ast::ExprPtr buildPostfix(rx::Parser::PostfixExpressionContext *ctx);

    // 各表达式入口共用后缀构建，将下标等操作包装到已有的 base 节点上。
    ast::ExprPtr buildPostfixSuffix(ast::ExprPtr base, rx::Parser::PostfixSuffixContext *ctx);

    ast::ExprPtr buildPrimary(rx::Parser::PrimaryExpressionContext *ctx);

    ast::ExprPtr buildPath(rx::Parser::PathInExpressionContext *ctx);

    ast::ExprPtr buildStatementExpression(rx::Parser::StatementExpressionContext *ctx);

    ast::ExprPtr buildStatementAdditive(rx::Parser::StatementAdditiveExpressionContext *ctx);

    ast::ExprPtr buildStatementMultiplicative(rx::Parser::StatementMultiplicativeExpressionContext *ctx);
    
    ast::ExprPtr buildStatementCast(rx::Parser::StatementCastExpressionContext *ctx);

    ast::ExprPtr buildStatementUnary(rx::Parser::StatementUnaryExpressionContext *ctx);

    ast::ExprPtr buildStatementPostfix(rx::Parser::StatementPostfixExpressionContext *ctx);

    // 两套表达式入口共用的底层转换。
    ast::ExprPtr buildNonBlockPrimary(rx::Parser::NonBlockPrimaryContext *ctx);

    // Boolean operators, comparisons, closed operands and if expressions.
    ast::ExprPtr buildConditionExpression(rx::Parser::ConditionExpressionContext *ctx);

    //No prefix: Ordinary expression
    ast::ExprPtr buildBitOr(rx::Parser::BitOrExpressionContext *ctx);

    ast::ExprPtr buildBitXor(rx::Parser::BitXorExpressionContext *ctx);

    ast::ExprPtr buildBitAnd(rx::Parser::BitAndExpressionContext *ctx);

    ast::ExprPtr buildShift(rx::Parser::ShiftExpressionContext *ctx);

    //The "closed" prefix: handles ambiguity between < and generic parameters
    ast::ExprPtr buildClosedBitOr(rx::Parser::ClosedBitOrExpressionContext *ctx);

    ast::ExprPtr buildClosedBitXor(rx::Parser::ClosedBitXorExpressionContext *ctx);

    ast::ExprPtr buildClosedBitAnd(rx::Parser::ClosedBitAndExpressionContext *ctx);

    ast::ExprPtr buildClosedShift(rx::Parser::ClosedShiftExpressionContext *ctx);

    ast::ExprPtr buildLogicalOr(rx::Parser::LogicalOrExpressionContext *ctx);

    ast::ExprPtr buildLogicalAnd(rx::Parser::LogicalAndExpressionContext *ctx);

    ast::ExprPtr buildComparison(rx::Parser::ComparisonExpressionContext *ctx);

    ast::ExprPtr buildClosedAdditive(rx::Parser::ClosedAdditiveExpressionContext *ctx);

    ast::ExprPtr buildClosedMultiplicative(rx::Parser::ClosedMultiplicativeExpressionContext *ctx);

    ast::ExprPtr buildClosedCast(rx::Parser::ClosedCastExpressionContext *ctx);

    //statement prefix: The expression that enters from the beginning of the statement.
    ast::ExprPtr buildStatementBitOr(rx::Parser::StatementBitOrExpressionContext *ctx);

    ast::ExprPtr buildStatementBitXor(rx::Parser::StatementBitXorExpressionContext *ctx);

    ast::ExprPtr buildStatementBitAnd(rx::Parser::StatementBitAndExpressionContext *ctx);

    ast::ExprPtr buildStatementShift(rx::Parser::StatementShiftExpressionContext *ctx);

    ast::ExprPtr buildStatementClosedBitOr(rx::Parser::StatementClosedBitOrExpressionContext *ctx);

    ast::ExprPtr buildStatementClosedBitXor(rx::Parser::StatementClosedBitXorExpressionContext *ctx);

    ast::ExprPtr buildStatementClosedBitAnd(rx::Parser::StatementClosedBitAndExpressionContext *ctx);

    ast::ExprPtr buildStatementClosedShift(rx::Parser::StatementClosedShiftExpressionContext *ctx);

    ast::ExprPtr buildStatementLogicalOr(rx::Parser::StatementLogicalOrExpressionContext *ctx);

    ast::ExprPtr buildStatementLogicalAnd(rx::Parser::StatementLogicalAndExpressionContext *ctx);

    ast::ExprPtr buildStatementComparison(rx::Parser::StatementComparisonExpressionContext *ctx);

    ast::ExprPtr buildStatementClosedAdditive(rx::Parser::StatementClosedAdditiveExpressionContext *ctx);

    ast::ExprPtr buildStatementClosedMultiplicative(rx::Parser::StatementClosedMultiplicativeExpressionContext *ctx);

    ast::ExprPtr buildStatementClosedCast(rx::Parser::StatementClosedCastExpressionContext *ctx);

    //condition prefix: The expression in the condition position
    ast::ExprPtr buildConditionBitOr(rx::Parser::ConditionBitOrExpressionContext *ctx);

    ast::ExprPtr buildConditionBitXor(rx::Parser::ConditionBitXorExpressionContext *ctx);

    ast::ExprPtr buildConditionBitAnd(rx::Parser::ConditionBitAndExpressionContext *ctx);

    ast::ExprPtr buildConditionShift(rx::Parser::ConditionShiftExpressionContext *ctx);

    ast::ExprPtr buildConditionClosedBitOr(rx::Parser::ConditionClosedBitOrExpressionContext *ctx);

    ast::ExprPtr buildConditionClosedBitXor(rx::Parser::ConditionClosedBitXorExpressionContext *ctx);

    ast::ExprPtr buildConditionClosedBitAnd(rx::Parser::ConditionClosedBitAndExpressionContext *ctx);

    ast::ExprPtr buildConditionClosedShift(rx::Parser::ConditionClosedShiftExpressionContext *ctx);

    ast::ExprPtr buildConditionLogicalOr(rx::Parser::ConditionLogicalOrExpressionContext *ctx);

    ast::ExprPtr buildConditionLogicalAnd(rx::Parser::ConditionLogicalAndExpressionContext *ctx);

    ast::ExprPtr buildConditionComparison(rx::Parser::ConditionComparisonExpressionContext *ctx);

    ast::ExprPtr buildConditionClosedAdditive(rx::Parser::ConditionClosedAdditiveExpressionContext *ctx);

    ast::ExprPtr buildConditionClosedMultiplicative(rx::Parser::ConditionClosedMultiplicativeExpressionContext *ctx);

    ast::ExprPtr buildConditionClosedCast(rx::Parser::ConditionClosedCastExpressionContext *ctx);

    ast::ExprPtr buildConditionAdditive(rx::Parser::ConditionAdditiveExpressionContext *ctx);

    ast::ExprPtr buildConditionMultiplicative(rx::Parser::ConditionMultiplicativeExpressionContext *ctx);

    ast::ExprPtr buildConditionCast(rx::Parser::ConditionCastExpressionContext *ctx);

    ast::ExprPtr buildConditionUnary(rx::Parser::ConditionUnaryExpressionContext *ctx);

    ast::ExprPtr buildConditionPostfix(rx::Parser::ConditionPostfixExpressionContext *ctx);

    ast::ExprPtr buildConditionPrimary(rx::Parser::ConditionPrimaryContext *ctx);

    ast::ExprPtr buildConditionPrimaryWithoutBareBlock(rx::Parser::ConditionPrimaryWithoutBareBlockContext *ctx);

    // conditionBreak 前缀：break 在条件位置的值不能直接以裸块开头。
    ast::ExprPtr buildConditionBreakExpression(rx::Parser::ConditionBreakExpressionContext *ctx);

    ast::ExprPtr buildConditionBreakLogicalOr(rx::Parser::ConditionBreakLogicalOrExpressionContext *ctx);

    ast::ExprPtr buildConditionBreakLogicalAnd(rx::Parser::ConditionBreakLogicalAndExpressionContext *ctx);

    ast::ExprPtr buildConditionBreakComparison(rx::Parser::ConditionBreakComparisonExpressionContext *ctx);

    ast::ExprPtr buildConditionBreakBitOr(rx::Parser::ConditionBreakBitOrExpressionContext *ctx);

    ast::ExprPtr buildConditionBreakBitXor(rx::Parser::ConditionBreakBitXorExpressionContext *ctx);

    ast::ExprPtr buildConditionBreakBitAnd(rx::Parser::ConditionBreakBitAndExpressionContext *ctx);

    ast::ExprPtr buildConditionBreakShift(rx::Parser::ConditionBreakShiftExpressionContext *ctx);

    ast::ExprPtr buildConditionBreakClosedBitOr(rx::Parser::ConditionBreakClosedBitOrExpressionContext *ctx);

    ast::ExprPtr buildConditionBreakClosedBitXor(rx::Parser::ConditionBreakClosedBitXorExpressionContext *ctx);

    ast::ExprPtr buildConditionBreakClosedBitAnd(rx::Parser::ConditionBreakClosedBitAndExpressionContext *ctx);

    ast::ExprPtr buildConditionBreakClosedShift(rx::Parser::ConditionBreakClosedShiftExpressionContext *ctx);

    ast::ExprPtr buildConditionBreakAdditive(rx::Parser::ConditionBreakAdditiveExpressionContext *ctx);

    ast::ExprPtr buildConditionBreakMultiplicative(rx::Parser::ConditionBreakMultiplicativeExpressionContext *ctx);

    ast::ExprPtr buildConditionBreakCast(rx::Parser::ConditionBreakCastExpressionContext *ctx);

    ast::ExprPtr buildConditionBreakClosedAdditive(rx::Parser::ConditionBreakClosedAdditiveExpressionContext *ctx);

    ast::ExprPtr buildConditionBreakClosedMultiplicative(rx::Parser::ConditionBreakClosedMultiplicativeExpressionContext *ctx);

    ast::ExprPtr buildConditionBreakClosedCast(rx::Parser::ConditionBreakClosedCastExpressionContext *ctx);

    ast::ExprPtr buildConditionBreakUnary(rx::Parser::ConditionBreakUnaryExpressionContext *ctx);

    ast::ExprPtr buildConditionBreakPostfix(rx::Parser::ConditionBreakPostfixExpressionContext *ctx);

    ast::ExprPtr buildIf(rx::Parser::IfExpressionContext *ctx);

    // loop 在两条语法规则中出现，共用循环体的构建逻辑。
    ast::ExprPtr buildLoop(rx::Parser::BlockExpressionContext *ctx);

    // while 在两条语法规则中出现，共用条件和循环体的构建逻辑。
    ast::ExprPtr buildWhile(rx::Parser::ConditionExpressionContext *conditionCtx, rx::Parser::BlockExpressionContext *blockCtx);

    ast::ExprPtr buildExpressionWithBlock(rx::Parser::ExpressionWithBlockContext *ctx);

    std::unique_ptr<ast::FunctionParam> buildNamedParam(rx::Parser::FunctionParamContext *ctx);

    std::unique_ptr<ast::TypeRef> buildTypeRef(rx::Parser::TypeRefContext *ctx);

    // [T; N] 的 T 走类型入口，N 走常量表达式入口。
    std::unique_ptr<ast::ArrayTypeRef> buildArrayType(rx::Parser::ArrayTypeContext *ctx);

    std::unique_ptr<ast::SelfFunctionParam> buildSelfParam(rx::Parser::SelfParamContext *ctx);
    //普通参数
    std::unique_ptr<ast::FunctionParam> buildFunctionParam(rx::Parser::FunctionParamContext *ctx);
public:
    std::unique_ptr<ast::Crate> build(rx::Parser::CrateContext *ctx);
};
}
