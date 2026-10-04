#include"ast_builder.hpp"

#include <stdexcept>

namespace {

// 本次只允许穿过单孩子的表达式规则。
void requireSingleChild(antlr4::ParserRuleContext *ctx) {
    if (ctx == nullptr || ctx->children.size() != 1) {
        throw std::runtime_error(
           "this expression form is not supported yet"
        );
    }
}

// Lexer 已验证进制、下划线和类型后缀；各表达式入口共用节点构建。
rx::ast::ExprPtr buildIntegerLiteral(antlr4::tree::TerminalNode *integer) {
    // 保留完整原文，后续语义分析再按进制求值并处理类型后缀及范围。
    return std::make_unique<rx::ast::IntegerLiteralExpr>(integer->getText());
}

// 优先级已由解析树确定；这里只把同一层的左结合运算依次折叠成 BinaryExpr。
// 各入口通过 buildRight 指定后续操作数的构建方式，包括 closed 的最后一项。
template<class OperatorContext, class BuildRight>
rx::ast::ExprPtr buildBinaryChain(rx::ast::ExprPtr result, const std::vector<OperatorContext*> &operators, BuildRight buildRight){
    for(std::size_t i = 0; i < operators.size(); ++i){
        std::string op = operators[i]->getText();
        auto right = buildRight(i);

        result = std::make_unique<rx::ast::BinaryExpr>(
            std::move(op),
            std::move(result),
            std::move(right)
        );
    }
    return result;
}

// 比较规则最多有一个运算符；没有运算符时直接返回左侧，不构建右侧。
template<class OperatorContext, class BuildRight>
rx::ast::ExprPtr buildOptionalBinary(rx::ast::ExprPtr left, OperatorContext *op, BuildRight buildRight){
    if(op == nullptr){
        return left;
    }

    std::string text = op->getText();
    auto right = buildRight();
    return std::make_unique<rx::ast::BinaryExpr>(
        std::move(text),
        std::move(left),
        std::move(right)
    );
}

// 赋值的右结合性由右侧 expression 的递归构建保留，具体入口由回调指定。
template<class BuildRight>
rx::ast::ExprPtr buildAssignment(rx::ast::ExprPtr left, rx::Parser::AssignmentOperatorContext *op, BuildRight buildRight){
    if(op == nullptr){
        return left;
    }
    if(op->equalsSign() == nullptr){
        throw std::runtime_error{"compound assignment is not supported yet"};
    }

    auto right = buildRight();
    return std::make_unique<rx::ast::AssignExpr>(
        std::move(left),
        std::move(right)
    );
}

} // namespace

namespace rx::frontend{

std::unique_ptr<ast::Crate> ASTBuilder::build(rx::Parser::CrateContext *ctx){
    auto result = std::make_unique<ast::Crate>();
    for(auto itemCtx : ctx->item()){
        result->addItem(buildItem(itemCtx));
    }
    return result;
}
    
std::unique_ptr<ast::Item> ASTBuilder::buildItem(rx::Parser::ItemContext *ctx){
    if (ctx->functionDefinition() != nullptr) {
        return buildFunction(ctx->functionDefinition());
    }

    if (ctx->constantItem() != nullptr) {
        return buildConstItem(ctx->constantItem());
    }

    throw std::runtime_error(
        "this item form is not supported yet"
    );
}

std::unique_ptr<ast::ConstItem> ASTBuilder::buildConstItem(rx::Parser::ConstantItemContext *ctx) {
    std::string name = ctx->identifier()->getText();
    auto type = buildTypeRef(ctx->typeRef());
    auto value = buildConstValue(ctx->constValue());

    return std::make_unique<ast::ConstItem>(
        std::move(name),
        std::move(type),
        std::move(value)
    );
}

std::unique_ptr<ast::FunctionItem> ASTBuilder::buildFunction(rx::Parser::FunctionDefinitionContext *ctx){
    std::string name = ctx->identifier()->getText();

    std::unique_ptr<ast::SelfFunctionParam> selfParam;
    std::vector<std::unique_ptr<ast::FunctionParam>> parameters;

    if (auto *params = ctx->functionParameters()) {
        if (params->selfParam() != nullptr) {
            selfParam = buildSelfParam(
                params->selfParam()
            );
        }

        for (auto *param : params->functionParam()) {
            parameters.push_back(buildNamedParam(param));
        }
    }

    std::unique_ptr<ast::TypeRef> returnType;

    if (ctx->typeRef() != nullptr) {
        returnType = buildTypeRef(ctx->typeRef());
    }

    auto body = buildBlock(ctx->blockExpression());

    return std::make_unique<ast::FunctionItem>(
        std::move(name),
        std::move(selfParam),
        std::move(parameters),
        std::move(returnType),
        std::move(body)
    );
}

std::unique_ptr<ast::BlockExpr> ASTBuilder::buildBlock(rx::Parser::BlockExpressionContext *ctx){
    auto block = std::make_unique<ast::BlockExpr>();

    for (auto statementCtx : ctx->statement()) {
        auto statement = buildStatement(statementCtx);
        block->addStatement(std::move(statement));
    }
    if( ctx->statementExpression() != nullptr){
        block->setTail(buildStatementExpression(ctx->statementExpression()));
    }
    

    return block;
}

std::unique_ptr<ast::LetStmt> ASTBuilder::buildLet(rx::Parser::LetStatementContext *ctx){
    auto *binding = ctx->identifierBinding();

    if (ctx->typeRef() != nullptr) {
        throw std::runtime_error("type annotations are not supported yet");
    }

    std::string name = binding->identifier()->getText();
    bool isMutable = binding->MUT() != nullptr;
    //Recursively construct the initialization expression
    auto initializer = buildExpression(ctx->expression());

    return std::make_unique<ast::LetStmt>(std::move(name), isMutable, std::move(initializer));
}

ast::StmtPtr ASTBuilder::buildStatement(rx::Parser::StatementContext *ctx){
    if(ctx->letStatement() != nullptr){
        return buildLet(ctx->letStatement());
    }

    if(ctx->statementExpression() != nullptr){
        auto expression = buildStatementExpression(ctx->statementExpression());
        return std::make_unique<ast::ExprStmt>(std::move(expression));
    }

    if(ctx->expressionWithBlock() != nullptr){
        // 带块表达式共用一个分派入口，在语句位置统一包装为 ExprStmt。
        auto expression = buildExpressionWithBlock(ctx->expressionWithBlock());
        return std::make_unique<ast::ExprStmt>(std::move(expression));
    }

    throw std::runtime_error("this statement form is not supported yet");
}

ast::ExprPtr ASTBuilder::buildExpression(rx::Parser::ExpressionContext *ctx){
    auto assignment = ctx->assignmentExpression();
    return buildAssignment(
        buildLogicalOr(assignment->logicalOrExpression()),  //left
        assignment->assignmentOperator(),                   //op
        [&]{                                                //buildright
            return buildExpression(assignment->expression());
        }
    );
}

ast::ExprPtr ASTBuilder::buildLiteral(rx::Parser::LiteralExpressionContext *ctx){
    if(ctx->TRUE() != nullptr || ctx->FALSE() != nullptr){
        bool flag = ctx->TRUE() != nullptr;

        return std::make_unique<ast::BooleanLiteralExpr>(flag);
    }

    auto integer = ctx->INTEGER_LITERAL();
    if(integer != nullptr){
        return buildIntegerLiteral(integer);
    }

    throw std::runtime_error(
        "only integer and boolean literals are supported for now"
    );
}

ast::ExprPtr ASTBuilder::buildArray(rx::Parser::ArrayExpressionContext *ctx){
    if(ctx->SEMI() != nullptr){
        auto value = buildExpression(ctx->expression(0));
        auto count = buildConstValue(ctx->constValue());
        return std::make_unique<ast::ArrayRepeatExpr>(
            std::move(value),
            std::move(count)
        );
    }

    // [a, b, ...]，也包含空数组 []
    std::vector<ast::ExprPtr> elements;
    for (auto *elementCtx : ctx->expression()) {
        elements.push_back(buildExpression(elementCtx));
    }

    return std::make_unique<ast::ArrayExpr>(
        std::move(elements)
    );
}

ast::ExprPtr ASTBuilder::buildConstValue(rx::Parser::ConstValueContext *ctx){
    if (ctx->MINUS() != nullptr) {
        auto operand = buildMagnitude(ctx->magnitude());

        return std::make_unique<ast::UnaryExpr>(
            "-",
            std::move(operand)
        );
    }

    if(ctx->pathInExpression() != nullptr){ //用现成的build函数，不要试图自己重写
        // return std::make_unique<ast::PathExpr>(
        //     std::move(ctx->pathInExpression()->pathExprSegment());
        // )
        return buildPath(ctx->pathInExpression());
    }
    if(ctx->INTEGER_LITERAL() != nullptr){
        return buildIntegerLiteral(ctx->INTEGER_LITERAL());
    }
    if(ctx->TRUE() != nullptr || ctx->FALSE() != nullptr){
        return std::make_unique<ast::BooleanLiteralExpr>(ctx->TRUE() != nullptr);
    }
    if(ctx->LPAREN() != nullptr){
        return buildConstValue(ctx->constValue());
    }
    throw std::runtime_error{"this constant value is not supported yet"};
}

ast::ExprPtr ASTBuilder::buildMagnitude(rx::Parser::MagnitudeContext *ctx){
    if(ctx->INTEGER_LITERAL() != nullptr){
        return buildIntegerLiteral(ctx->INTEGER_LITERAL());
    }

    if(ctx->pathInExpression() != nullptr){
        return buildPath(ctx->pathInExpression());
    }

    if(ctx->LPAREN() != nullptr){
        return buildMagnitude(ctx->magnitude());
    }

    throw std::runtime_error{"this magnitude is not supported yet"};
}

ast::ExprPtr ASTBuilder::buildAdditive(rx::Parser::AdditiveExpressionContext *ctx){
    auto operands = ctx->multiplicativeExpression();
    return buildBinaryChain(
        buildMultiplicative(operands[0]),
        ctx->additiveOperator(),
        [&](std::size_t i){
            return buildMultiplicative(operands[i + 1]);
        }
    );
}

ast::ExprPtr ASTBuilder::buildMultiplicative(rx::Parser::MultiplicativeExpressionContext *ctx) {
    auto operands = ctx->castExpression();
    return buildBinaryChain(
        buildCast(operands[0]),
        ctx->multiplicativeOperator(),
        [&](std::size_t i){
            return buildCast(operands[i + 1]);
        }
    );
}

ast::ExprPtr ASTBuilder::buildCast(rx::Parser::CastExpressionContext *ctx) {
    if (!ctx->typeRef().empty()) {
        throw std::runtime_error("as casts are not supported yet");
    }

    return buildUnary(ctx->unaryExpression());
}

ast::ExprPtr ASTBuilder::buildUnary(rx::Parser::UnaryExpressionContext *ctx){
    //two branches 1)has prefix operator, build recursively  2) otherwise give it to next floor postfixExpression directly
    if(ctx->unaryOperator() != nullptr){
        std::string op = ctx->unaryOperator()->getText();

        // std::cerr << op << '\n';

        // 本次支持取负和取反。
        // 解引用、借用等操作留到后面。
        if(op != "-" && op != "!"){
            throw std::runtime_error{"this unary is not supported yet"};
        }

        auto operand = buildUnary(ctx->unaryExpression());

        return std::make_unique<ast::UnaryExpr>(
            std::move(op),
            std::move(operand)
        );
    }
    return buildPostfix(ctx->postfixExpression());
}

ast::ExprPtr ASTBuilder::buildPostfix(rx::Parser::PostfixExpressionContext *ctx){
    auto result = buildPrimary(ctx->primaryExpression());
    for (auto *suffix : ctx->postfixSuffix()) {
        result = buildPostfixSuffix(std::move(result), suffix);
    }
    return result;
}

ast::ExprPtr ASTBuilder::buildPostfixSuffix(ast::ExprPtr base, rx::Parser::PostfixSuffixContext *ctx){
    if(ctx->LBRACKET() != nullptr){
        auto index = buildExpression(ctx->expression());
        return std::make_unique<ast::IndexExpr>(
            std::move(base),
            std::move(index)
        );
    }

    throw std::runtime_error{"this post suffix is not supported yet"};
}

ast::ExprPtr ASTBuilder::buildPrimary(rx::Parser::PrimaryExpressionContext *ctx) {
    auto nonBlock = ctx->nonBlockPrimary();

    if (nonBlock != nullptr) {
        return buildNonBlockPrimary(nonBlock);
    }

    auto withBlock = ctx->expressionWithBlock();

    if (withBlock != nullptr) {
        return buildExpressionWithBlock(withBlock);
    }

    throw std::runtime_error{
        "unsupported primary expression"
    };
}

ast::ExprPtr ASTBuilder::buildPath(rx::Parser::PathInExpressionContext *ctx){
    auto segmentContexts = ctx->pathExprSegment();

    //just support single path for the time being
    if(segmentContexts.size() != 1){
        throw std::runtime_error{
            "only single-segment paths are supported for now"
        };
    }
    auto segmentCtx = segmentContexts[0];
    // 暂不支持带泛型参数的路径，例如 foo::<i32>。
    if (segmentCtx->genericArgs() != nullptr) {
        throw std::runtime_error(
            "generic arguments in paths are not supported yet"
        );
    }

    auto identCtx = segmentCtx->pathIdentSegment();

    // pathIdentSegment 也允许 self 和 Self。
    // 本关只接受普通 identifier。
    if (identCtx->identifier() == nullptr) {
        throw std::runtime_error(
            "self and Self paths are not supported yet"
        );
    }

    std::string name = identCtx->identifier()->getText();

    std::vector<std::string> segments;
    segments.push_back(std::move(name));

    return std::make_unique<ast::PathExpr>(
        std::move(segments)
    );
}

ast::ExprPtr ASTBuilder::buildStatementExpression(rx::Parser::StatementExpressionContext *ctx){
    auto assignment = ctx->statementAssignmentExpression();
    return buildAssignment(
        buildStatementLogicalOr(assignment->statementLogicalOrExpression()),
        assignment->assignmentOperator(),
        [&]{
            return buildExpression(assignment->expression());
        }
    );
}

ast::ExprPtr ASTBuilder::buildStatementAdditive(rx::Parser::StatementAdditiveExpressionContext *ctx) {
    auto operands = ctx->multiplicativeExpression();
    return buildBinaryChain(
        buildStatementMultiplicative(ctx->statementMultiplicativeExpression()),
        ctx->additiveOperator(),
        [&](std::size_t i){
            return buildMultiplicative(operands[i]);
        }
    );
}

ast::ExprPtr ASTBuilder::buildStatementMultiplicative(rx::Parser::StatementMultiplicativeExpressionContext *ctx) {
    auto operands = ctx->castExpression();
    return buildBinaryChain(
        buildStatementCast(ctx->statementCastExpression()),
        ctx->multiplicativeOperator(),
        [&](std::size_t i){
            return buildCast(operands[i]);
        }
    );
}

ast::ExprPtr ASTBuilder::buildStatementCast(rx::Parser::StatementCastExpressionContext *ctx) {
    if (!ctx->typeRef().empty()) {
        throw std::runtime_error(
            "as casts are not supported yet"
        );
    }

    return buildStatementUnary(ctx->statementUnaryExpression());
}

ast::ExprPtr ASTBuilder::buildStatementUnary(rx::Parser::StatementUnaryExpressionContext *ctx) {
    if (ctx->unaryOperator() != nullptr) {
        std::string op = ctx->unaryOperator()->getText();

        if (op != "-" && op != "!") {
            throw std::runtime_error(
                "this unary operator is not supported yet"
            );
        }

        // 语法规定：前缀运算符后面使用普通 unaryExpression。
        auto operand = buildUnary(ctx->unaryExpression());

        return std::make_unique<ast::UnaryExpr>(
            std::move(op),
            std::move(operand)
        );
    }

    return buildStatementPostfix(
        ctx->statementPostfixExpression()
    );
}

ast::ExprPtr ASTBuilder::buildStatementPostfix(rx::Parser::StatementPostfixExpressionContext *ctx) {
    if (ctx->expressionWithBlock() != nullptr ||
        ctx->dotSuffix() != nullptr) {
        throw std::runtime_error(
            "block-leading or postfix expressions are not supported yet"
        );
    }

    auto result = buildNonBlockPrimary(ctx->nonBlockPrimary());
    for (auto *suffix : ctx->postfixSuffix()) {
        result = buildPostfixSuffix(std::move(result), suffix);
    }
    return result;
}

ast::ExprPtr ASTBuilder::buildNonBlockPrimary(rx::Parser::NonBlockPrimaryContext *ctx){
    if (ctx == nullptr) {
        throw std::runtime_error(
            "expected a non-block primary expression"
        );
    }

    if (ctx->literalExpression() != nullptr) {
        return buildLiteral(ctx->literalExpression());
    }

    if (ctx->pathInExpression() != nullptr) {
        if (ctx->LBRACE() != nullptr) {
            throw std::runtime_error(
                "struct construction is not supported yet"
            );
        }

        return buildPath(ctx->pathInExpression());
    }
    //对于高维数组，又会跳回到buildArray重新构建里层的低维数组  一直递归下去
    if (ctx->arrayExpression() != nullptr) {
        return buildArray(ctx->arrayExpression());
    }

    // 语句入口也会走到这里，由 buildStatement 在外层包装 ExprStmt。
    if(ctx->CONTINUE() != nullptr){
        return std::make_unique<ast::ContinueExpr>();
    }

    if(ctx->BREAK() != nullptr){
        ast::ExprPtr value;
        // 普通位置允许 break 后面跟完整表达式，也允许省略值。
        if(ctx->expression() != nullptr){
            value = buildExpression(ctx->expression());
        }

        return std::make_unique<ast::BreakExpr>(
            std::move(value)
        );
    }

    if(ctx->RETURN() != nullptr){
        ast::ExprPtr value;

        if (ctx->expression() != nullptr) {
            value = buildExpression(
                ctx->expression()
            );
        }

        return std::make_unique<ast::ReturnExpr>(
            std::move(value)
        ); 
    }

    if (ctx->LPAREN() != nullptr) {
        auto *inner = ctx->expression();

        if (inner == nullptr) {
            throw std::runtime_error(
                "unit expression () is not supported yet"
            );
        }

        return buildExpression(inner);
    }

    throw std::runtime_error(
        "this primary expression is not supported yet"
    );
}
    // Boolean operators, comparisons, closed operands and if expressions.
ast::ExprPtr ASTBuilder::buildConditionExpression(rx::Parser::ConditionExpressionContext *ctx){
    auto assignment = ctx->conditionAssignmentExpression();
    return buildAssignment(
        buildConditionLogicalOr(assignment->conditionLogicalOrExpression()),
        assignment->assignmentOperator(),
        [&]{
            return buildConditionExpression(assignment->conditionExpression());
        }
    );
}

ast::ExprPtr ASTBuilder::buildBitOr(rx::Parser::BitOrExpressionContext *ctx){
    requireSingleChild(ctx);
    return buildBitXor(ctx->bitXorExpression(0));
}

ast::ExprPtr ASTBuilder::buildBitXor(rx::Parser::BitXorExpressionContext *ctx){
    requireSingleChild(ctx);
    return buildBitAnd(ctx->bitAndExpression(0));
}

ast::ExprPtr ASTBuilder::buildBitAnd(rx::Parser::BitAndExpressionContext *ctx){
    requireSingleChild(ctx);
    return buildShift(ctx->shiftExpression(0));
}

ast::ExprPtr ASTBuilder::buildShift(rx::Parser::ShiftExpressionContext *ctx){
    requireSingleChild(ctx);
    return buildAdditive(ctx->additiveExpression(0));
}

ast::ExprPtr ASTBuilder::buildClosedBitOr(rx::Parser::ClosedBitOrExpressionContext *ctx){
    requireSingleChild(ctx);
    return buildClosedBitXor(ctx->closedBitXorExpression());
}

ast::ExprPtr ASTBuilder::buildClosedBitXor(rx::Parser::ClosedBitXorExpressionContext *ctx){
    requireSingleChild(ctx);
    return buildClosedBitAnd(ctx->closedBitAndExpression());
}

ast::ExprPtr ASTBuilder::buildClosedBitAnd(rx::Parser::ClosedBitAndExpressionContext *ctx){
    requireSingleChild(ctx);
    return buildClosedShift(ctx->closedShiftExpression());
}

ast::ExprPtr ASTBuilder::buildClosedShift(rx::Parser::ClosedShiftExpressionContext *ctx){
    requireSingleChild(ctx);
    return buildClosedAdditive(ctx->closedAdditiveExpression(0));
}

ast::ExprPtr ASTBuilder::buildLogicalOr(rx::Parser::LogicalOrExpressionContext *ctx){
    auto operands = ctx->logicalAndExpression();
    return buildBinaryChain(
        buildLogicalAnd(operands[0]),
        ctx->OROR(),
        [&](std::size_t i){
            return buildLogicalAnd(operands[i + 1]);
        }
    );
}

ast::ExprPtr ASTBuilder::buildLogicalAnd(rx::Parser::LogicalAndExpressionContext *ctx){
    auto operands = ctx->comparisonExpression();
    return buildBinaryChain(
        buildComparison(operands[0]),
        ctx->ANDAND(),
        [&](std::size_t i){
            return buildComparison(operands[i + 1]);
        }
    );
}

ast::ExprPtr ASTBuilder::buildComparison(rx::Parser::ComparisonExpressionContext *ctx){
    if(ctx->LT() != nullptr){
        return buildOptionalBinary(
            buildClosedBitOr(ctx->closedBitOrExpression()),
            ctx->LT(),
            [&]{
                return buildBitOr(ctx->bitOrExpression(0));
            }
        );
    }

    return buildOptionalBinary(
        buildBitOr(ctx->bitOrExpression(0)),
        ctx->comparisonExceptLt(),
        [&]{
            return buildBitOr(ctx->bitOrExpression(1));
        }
    );
}

ast::ExprPtr ASTBuilder::buildClosedAdditive(rx::Parser::ClosedAdditiveExpressionContext *ctx){
    auto operands = ctx->multiplicativeExpression();
    if(operands.empty()){
        return buildClosedMultiplicative(ctx->closedMultiplicativeExpression());
    }
    return buildBinaryChain(
        buildMultiplicative(operands[0]),
        ctx->additiveOperator(),
        [&](std::size_t i){
            if(i + 1 < operands.size()){
                return buildMultiplicative(operands[i + 1]);
            }
            // 最后一项保留 closed 入口，供外层的 < 正确解析。
            return buildClosedMultiplicative(ctx->closedMultiplicativeExpression());
        }
    );
}

ast::ExprPtr ASTBuilder::buildClosedMultiplicative(rx::Parser::ClosedMultiplicativeExpressionContext *ctx){
    auto operands = ctx->castExpression();
    if(operands.empty()){
        return buildClosedCast(ctx->closedCastExpression());
    }
    return buildBinaryChain(
        buildCast(operands[0]),
        ctx->multiplicativeOperator(),
        [&](std::size_t i){
            if(i + 1 < operands.size()){
                return buildCast(operands[i + 1]);
            }
            // 最后一项保留 closed 入口，供外层的 < 正确解析。
            return buildClosedCast(ctx->closedCastExpression());
        }
    );
}

ast::ExprPtr ASTBuilder::buildClosedCast(rx::Parser::ClosedCastExpressionContext *ctx){
    if(ctx->unaryExpression() == nullptr){
        throw std::runtime_error{"as casts are not supported yet"};
    }
    return buildUnary(ctx->unaryExpression());
}

//statement prefix: The expression that enters from the beginning of the statement.
ast::ExprPtr ASTBuilder::buildStatementBitOr(rx::Parser::StatementBitOrExpressionContext *ctx){
    requireSingleChild(ctx);
    return buildStatementBitXor(ctx->statementBitXorExpression());
}

ast::ExprPtr ASTBuilder::buildStatementBitXor(rx::Parser::StatementBitXorExpressionContext *ctx){
    requireSingleChild(ctx);
    return buildStatementBitAnd(ctx->statementBitAndExpression());
}

ast::ExprPtr ASTBuilder::buildStatementBitAnd(rx::Parser::StatementBitAndExpressionContext *ctx){
    requireSingleChild(ctx);
    return buildStatementShift(ctx->statementShiftExpression());
}

ast::ExprPtr ASTBuilder::buildStatementShift(rx::Parser::StatementShiftExpressionContext *ctx){
    requireSingleChild(ctx);
    return buildStatementAdditive(ctx->statementAdditiveExpression());
}

ast::ExprPtr ASTBuilder::buildStatementClosedBitOr(rx::Parser::StatementClosedBitOrExpressionContext *ctx){
    requireSingleChild(ctx);
    return buildStatementClosedBitXor(ctx->statementClosedBitXorExpression());
}

ast::ExprPtr ASTBuilder::buildStatementClosedBitXor(rx::Parser::StatementClosedBitXorExpressionContext *ctx){
    requireSingleChild(ctx);
    return buildStatementClosedBitAnd(ctx->statementClosedBitAndExpression());
}

ast::ExprPtr ASTBuilder::buildStatementClosedBitAnd(rx::Parser::StatementClosedBitAndExpressionContext *ctx){
    requireSingleChild(ctx);
    return buildStatementClosedShift(ctx->statementClosedShiftExpression());
}

ast::ExprPtr ASTBuilder::buildStatementClosedShift(rx::Parser::StatementClosedShiftExpressionContext *ctx){
    requireSingleChild(ctx);
    return buildStatementClosedAdditive(ctx->statementClosedAdditiveExpression());
}

ast::ExprPtr ASTBuilder::buildStatementLogicalOr(rx::Parser::StatementLogicalOrExpressionContext *ctx){
    auto operands = ctx->logicalAndExpression();
    return buildBinaryChain(
        buildStatementLogicalAnd(ctx->statementLogicalAndExpression()),
        ctx->OROR(),
        [&](std::size_t i){
            return buildLogicalAnd(operands[i]);
        }
    );
}

ast::ExprPtr ASTBuilder::buildStatementLogicalAnd(rx::Parser::StatementLogicalAndExpressionContext *ctx){
    auto operands = ctx->comparisonExpression();
    return buildBinaryChain(
        buildStatementComparison(ctx->statementComparisonExpression()),
        ctx->ANDAND(),
        [&](std::size_t i){
            return buildComparison(operands[i]);
        }
    );
}

ast::ExprPtr ASTBuilder::buildStatementComparison(rx::Parser::StatementComparisonExpressionContext *ctx){
    if(ctx->LT() != nullptr){
        return buildOptionalBinary(
            buildStatementClosedBitOr(ctx->statementClosedBitOrExpression()),
            ctx->LT(),
            [&]{
                return buildBitOr(ctx->bitOrExpression());
            }
        );
    }

    return buildOptionalBinary(
        buildStatementBitOr(ctx->statementBitOrExpression()),
        ctx->comparisonExceptLt(),
        [&]{
            return buildBitOr(ctx->bitOrExpression());
        }
    );
}

ast::ExprPtr ASTBuilder::buildStatementClosedAdditive(rx::Parser::StatementClosedAdditiveExpressionContext *ctx){
    if(ctx->statementClosedMultiplicativeExpression() != nullptr){
        return buildStatementClosedMultiplicative(ctx->statementClosedMultiplicativeExpression());
    }

    auto operands = ctx->multiplicativeExpression();
    return buildBinaryChain(
        buildStatementMultiplicative(ctx->statementMultiplicativeExpression()),
        ctx->additiveOperator(),
        [&](std::size_t i){
            if(i < operands.size()){
                return buildMultiplicative(operands[i]);
            }
            // 最后一项保留 closed 入口，供外层的 < 正确解析。
            return buildClosedMultiplicative(ctx->closedMultiplicativeExpression());
        }
    );
}

ast::ExprPtr ASTBuilder::buildStatementClosedMultiplicative(rx::Parser::StatementClosedMultiplicativeExpressionContext *ctx){
    if(ctx->statementClosedCastExpression() != nullptr){
        return buildStatementClosedCast(ctx->statementClosedCastExpression());
    }

    auto operands = ctx->castExpression();
    return buildBinaryChain(
        buildStatementCast(ctx->statementCastExpression()),
        ctx->multiplicativeOperator(),
        [&](std::size_t i){
            if(i < operands.size()){
                return buildCast(operands[i]);
            }
            // 最后一项保留 closed 入口，供外层的 < 正确解析。
            return buildClosedCast(ctx->closedCastExpression());
        }
    );
}

ast::ExprPtr ASTBuilder::buildStatementClosedCast(rx::Parser::StatementClosedCastExpressionContext *ctx){
    if(ctx->statementUnaryExpression() == nullptr){
        throw std::runtime_error{"as casts are not supported yet"};
    }
    return buildStatementUnary(ctx->statementUnaryExpression());
}

ast::ExprPtr ASTBuilder::buildConditionBitOr(rx::Parser::ConditionBitOrExpressionContext *ctx){
    requireSingleChild(ctx);
    return buildConditionBitXor(ctx->conditionBitXorExpression(0));
}

ast::ExprPtr ASTBuilder::buildConditionBitXor(rx::Parser::ConditionBitXorExpressionContext *ctx){
    requireSingleChild(ctx);
    return buildConditionBitAnd(ctx->conditionBitAndExpression(0));
}

ast::ExprPtr ASTBuilder::buildConditionBitAnd(rx::Parser::ConditionBitAndExpressionContext *ctx){
    requireSingleChild(ctx);
    return buildConditionShift(ctx->conditionShiftExpression(0));
}

ast::ExprPtr ASTBuilder::buildConditionShift(rx::Parser::ConditionShiftExpressionContext *ctx){
    requireSingleChild(ctx);
    return buildConditionAdditive(ctx->conditionAdditiveExpression(0));
}

ast::ExprPtr ASTBuilder::buildConditionAdditive(rx::Parser::ConditionAdditiveExpressionContext *ctx){
    auto operands = ctx->conditionMultiplicativeExpression();
    return buildBinaryChain(
        buildConditionMultiplicative(operands[0]),
        ctx->additiveOperator(),
        [&](std::size_t i){
            return buildConditionMultiplicative(operands[i + 1]);
        }
    );
}

ast::ExprPtr ASTBuilder::buildConditionMultiplicative(rx::Parser::ConditionMultiplicativeExpressionContext *ctx){
    auto operands = ctx->conditionCastExpression();
    return buildBinaryChain(
        buildConditionCast(operands[0]),
        ctx->multiplicativeOperator(),
        [&](std::size_t i){
            return buildConditionCast(operands[i + 1]);
        }
    );
}

ast::ExprPtr ASTBuilder::buildConditionCast(rx::Parser::ConditionCastExpressionContext *ctx){
    if (!ctx->typeRef().empty()) {
        throw std::runtime_error("as casts are not supported yet");
    }
    return buildConditionUnary(ctx->conditionUnaryExpression());
}

ast::ExprPtr ASTBuilder::buildConditionClosedBitOr(rx::Parser::ConditionClosedBitOrExpressionContext *ctx){
    requireSingleChild(ctx);
    return buildConditionClosedBitXor(ctx->conditionClosedBitXorExpression());
}

ast::ExprPtr ASTBuilder::buildConditionClosedBitXor(rx::Parser::ConditionClosedBitXorExpressionContext *ctx){
    requireSingleChild(ctx);
    return buildConditionClosedBitAnd(ctx->conditionClosedBitAndExpression());
}

ast::ExprPtr ASTBuilder::buildConditionClosedBitAnd(rx::Parser::ConditionClosedBitAndExpressionContext *ctx){
    requireSingleChild(ctx);
    return buildConditionClosedShift(ctx->conditionClosedShiftExpression());
}

ast::ExprPtr ASTBuilder::buildConditionClosedShift(rx::Parser::ConditionClosedShiftExpressionContext *ctx){
    requireSingleChild(ctx);
    return buildConditionClosedAdditive(ctx->conditionClosedAdditiveExpression(0));
}

ast::ExprPtr ASTBuilder::buildConditionLogicalOr(rx::Parser::ConditionLogicalOrExpressionContext *ctx){
    auto operands = ctx->conditionLogicalAndExpression();
    return buildBinaryChain(
        buildConditionLogicalAnd(operands[0]),
        ctx->OROR(),
        [&](std::size_t i){
            return buildConditionLogicalAnd(operands[i + 1]);
        }
    );
}

ast::ExprPtr ASTBuilder::buildConditionLogicalAnd(rx::Parser::ConditionLogicalAndExpressionContext *ctx){
    auto operands = ctx->conditionComparisonExpression();
    return buildBinaryChain(
        buildConditionComparison(operands[0]),
        ctx->ANDAND(),
        [&](std::size_t i){
            return buildConditionComparison(operands[i + 1]);
        }
    );
}


ast::ExprPtr ASTBuilder::buildConditionComparison(rx::Parser::ConditionComparisonExpressionContext *ctx){
    if(ctx->LT() != nullptr){
        return buildOptionalBinary(
            buildConditionClosedBitOr(ctx->conditionClosedBitOrExpression()),
            ctx->LT(),
            [&]{
                return buildConditionBitOr(ctx->conditionBitOrExpression(0));
            }
        );
    }

    return buildOptionalBinary(
        buildConditionBitOr(ctx->conditionBitOrExpression(0)),
        ctx->comparisonExceptLt(),
        [&]{
            return buildConditionBitOr(ctx->conditionBitOrExpression(1));
        }
    );
}

ast::ExprPtr ASTBuilder::buildConditionClosedAdditive(rx::Parser::ConditionClosedAdditiveExpressionContext *ctx){
    auto operands = ctx->conditionMultiplicativeExpression();
    if(operands.empty()){
        return buildConditionClosedMultiplicative(ctx->conditionClosedMultiplicativeExpression());
    }
    return buildBinaryChain(
        buildConditionMultiplicative(operands[0]),
        ctx->additiveOperator(),
        [&](std::size_t i){
            if(i + 1 < operands.size()){
                return buildConditionMultiplicative(operands[i + 1]);
            }
            // 最后一项保留 closed 入口，供外层的 < 正确解析。
            return buildConditionClosedMultiplicative(ctx->conditionClosedMultiplicativeExpression());
        }
    );
}

ast::ExprPtr ASTBuilder::buildConditionClosedMultiplicative(rx::Parser::ConditionClosedMultiplicativeExpressionContext *ctx){
    auto operands = ctx->conditionCastExpression();
    if(operands.empty()){
        return buildConditionClosedCast(ctx->conditionClosedCastExpression());
    }
    return buildBinaryChain(
        buildConditionCast(operands[0]),
        ctx->multiplicativeOperator(),
        [&](std::size_t i){
            if(i + 1 < operands.size()){
                return buildConditionCast(operands[i + 1]);
            }
            // 最后一项保留 closed 入口，供外层的 < 正确解析。
            return buildConditionClosedCast(ctx->conditionClosedCastExpression());
        }
    );
}

ast::ExprPtr ASTBuilder::buildConditionClosedCast(rx::Parser::ConditionClosedCastExpressionContext *ctx){
    if(ctx->conditionUnaryExpression() == nullptr){
        throw std::runtime_error{"as casts are not supported yet"};
    }
    return buildConditionUnary(ctx->conditionUnaryExpression());
}

ast::ExprPtr ASTBuilder::buildConditionUnary(rx::Parser::ConditionUnaryExpressionContext *ctx){
    if(ctx->unaryOperator() != nullptr){
        std::string op = ctx->unaryOperator()->getText();
        if(op != "!" && op != "-"){
            throw std::runtime_error{"this unary operator is not supported yet"};
        }

        auto operand = buildConditionUnary(ctx->conditionUnaryExpression());
        return std::make_unique<ast::UnaryExpr>(
            op,
            std::move(operand)
        );
    }
    
    return buildConditionPostfix(ctx->conditionPostfixExpression());
}

ast::ExprPtr ASTBuilder::buildConditionPostfix(rx::Parser::ConditionPostfixExpressionContext *ctx){
    auto result = buildConditionPrimary(ctx->conditionPrimary());
    for (auto *suffix : ctx->postfixSuffix()) {
        result = buildPostfixSuffix(std::move(result), suffix);
    }
    return result;
}

ast::ExprPtr ASTBuilder::buildConditionPrimary(rx::Parser::ConditionPrimaryContext *ctx){
    if(ctx->blockExpression() != nullptr){
        return buildBlock(ctx->blockExpression());
    }
    return buildConditionPrimaryWithoutBareBlock(ctx->conditionPrimaryWithoutBareBlock());
}

ast::ExprPtr ASTBuilder::buildConditionPrimaryWithoutBareBlock(rx::Parser::ConditionPrimaryWithoutBareBlockContext *ctx){
    // 条件位置也有独立的 while 和 loop 语法分支，AST 构建阶段统一处理。
    if(ctx->WHILE() != nullptr){
        return buildWhile(ctx->conditionExpression(), ctx->blockExpression());
    }
    if(ctx->LOOP() != nullptr){
        return buildLoop(ctx->blockExpression());
    }
    if(ctx->literalExpression() != nullptr){
        return buildLiteral(ctx->literalExpression());
    }
    if(ctx->pathInExpression() != nullptr){
        return buildPath(ctx->pathInExpression());
    }
    if(ctx->arrayExpression() != nullptr){
        return buildArray(ctx->arrayExpression());
    }
    if(ctx->LPAREN() != nullptr){
        if(ctx->expression() == nullptr){
            throw std::runtime_error{"unit expression () is not supported yet"};
        }
        return buildExpression(ctx->expression());
    }
    if(ctx->ifExpression() != nullptr){
        return buildIf(ctx->ifExpression());
    }
    if(ctx->CONTINUE() != nullptr){
        return std::make_unique<ast::ContinueExpr>();
    }
    if(ctx->BREAK() != nullptr){
        ast::ExprPtr value;
        // 使用专门的条件入口，避免把 if / while 后面的块当成 break 的值。
        if(ctx->conditionBreakExpression() != nullptr){
            value = buildConditionBreakExpression(ctx->conditionBreakExpression());
        }

        return std::make_unique<ast::BreakExpr>(
            std::move(value)
        );
    }
    if(ctx->RETURN() != nullptr){
        ast::ExprPtr value;
        if(ctx->conditionExpression() != nullptr){
            value = buildConditionExpression(ctx->conditionExpression());
        }
        
        return std::make_unique<ast::ReturnExpr>(
            std::move(value)
        ); 
    }
    throw std::runtime_error{"this condition primary is not supported yet"};
}

ast::ExprPtr ASTBuilder::buildConditionBreakExpression(rx::Parser::ConditionBreakExpressionContext *ctx){
    auto assignment = ctx->conditionBreakAssignmentExpression();
    return buildAssignment(
        buildConditionBreakLogicalOr(assignment->conditionBreakLogicalOrExpression()),
        assignment->assignmentOperator(),
        [&]{
            return buildConditionExpression(assignment->conditionExpression());
        }
    );
}

ast::ExprPtr ASTBuilder::buildConditionBreakLogicalOr(rx::Parser::ConditionBreakLogicalOrExpressionContext *ctx){
    auto operands = ctx->conditionLogicalAndExpression();
    return buildBinaryChain(
        buildConditionBreakLogicalAnd(ctx->conditionBreakLogicalAndExpression()),
        ctx->OROR(),
        [&](std::size_t i){
            return buildConditionLogicalAnd(operands[i]);
        }
    );
}

ast::ExprPtr ASTBuilder::buildConditionBreakLogicalAnd(rx::Parser::ConditionBreakLogicalAndExpressionContext *ctx){
    auto operands = ctx->conditionComparisonExpression();
    return buildBinaryChain(
        buildConditionBreakComparison(ctx->conditionBreakComparisonExpression()),
        ctx->ANDAND(),
        [&](std::size_t i){
            return buildConditionComparison(operands[i]);
        }
    );
}

ast::ExprPtr ASTBuilder::buildConditionBreakComparison(rx::Parser::ConditionBreakComparisonExpressionContext *ctx){
    if(ctx->LT() != nullptr){
        return buildOptionalBinary(
            buildConditionBreakClosedBitOr(ctx->conditionBreakClosedBitOrExpression()),
            ctx->LT(),
            [&]{
                return buildConditionBitOr(ctx->conditionBitOrExpression());
            }
        );
    }

    return buildOptionalBinary(
        buildConditionBreakBitOr(ctx->conditionBreakBitOrExpression()),
        ctx->comparisonExceptLt(),
        [&]{
            return buildConditionBitOr(ctx->conditionBitOrExpression());
        }
    );
}

// 与现有表达式入口保持一致，位运算和移位暂时只穿过单孩子规则。
ast::ExprPtr ASTBuilder::buildConditionBreakBitOr(rx::Parser::ConditionBreakBitOrExpressionContext *ctx){
    requireSingleChild(ctx);
    return buildConditionBreakBitXor(ctx->conditionBreakBitXorExpression());
}

ast::ExprPtr ASTBuilder::buildConditionBreakBitXor(rx::Parser::ConditionBreakBitXorExpressionContext *ctx){
    requireSingleChild(ctx);
    return buildConditionBreakBitAnd(ctx->conditionBreakBitAndExpression());
}

ast::ExprPtr ASTBuilder::buildConditionBreakBitAnd(rx::Parser::ConditionBreakBitAndExpressionContext *ctx){
    requireSingleChild(ctx);
    return buildConditionBreakShift(ctx->conditionBreakShiftExpression());
}

ast::ExprPtr ASTBuilder::buildConditionBreakShift(rx::Parser::ConditionBreakShiftExpressionContext *ctx){
    requireSingleChild(ctx);
    return buildConditionBreakAdditive(ctx->conditionBreakAdditiveExpression());
}

ast::ExprPtr ASTBuilder::buildConditionBreakClosedBitOr(rx::Parser::ConditionBreakClosedBitOrExpressionContext *ctx){
    requireSingleChild(ctx);
    return buildConditionBreakClosedBitXor(ctx->conditionBreakClosedBitXorExpression());
}

ast::ExprPtr ASTBuilder::buildConditionBreakClosedBitXor(rx::Parser::ConditionBreakClosedBitXorExpressionContext *ctx){
    requireSingleChild(ctx);
    return buildConditionBreakClosedBitAnd(ctx->conditionBreakClosedBitAndExpression());
}

ast::ExprPtr ASTBuilder::buildConditionBreakClosedBitAnd(rx::Parser::ConditionBreakClosedBitAndExpressionContext *ctx){
    requireSingleChild(ctx);
    return buildConditionBreakClosedShift(ctx->conditionBreakClosedShiftExpression());
}

ast::ExprPtr ASTBuilder::buildConditionBreakClosedShift(rx::Parser::ConditionBreakClosedShiftExpressionContext *ctx){
    requireSingleChild(ctx);
    return buildConditionBreakClosedAdditive(ctx->conditionBreakClosedAdditiveExpression());
}

ast::ExprPtr ASTBuilder::buildConditionBreakAdditive(rx::Parser::ConditionBreakAdditiveExpressionContext *ctx){
    auto operands = ctx->conditionMultiplicativeExpression();
    return buildBinaryChain(
        buildConditionBreakMultiplicative(ctx->conditionBreakMultiplicativeExpression()),
        ctx->additiveOperator(),
        [&](std::size_t i){
            return buildConditionMultiplicative(operands[i]);
        }
    );
}

ast::ExprPtr ASTBuilder::buildConditionBreakMultiplicative(rx::Parser::ConditionBreakMultiplicativeExpressionContext *ctx){
    auto operands = ctx->conditionCastExpression();
    return buildBinaryChain(
        buildConditionBreakCast(ctx->conditionBreakCastExpression()),
        ctx->multiplicativeOperator(),
        [&](std::size_t i){
            return buildConditionCast(operands[i]);
        }
    );
}

ast::ExprPtr ASTBuilder::buildConditionBreakCast(rx::Parser::ConditionBreakCastExpressionContext *ctx){
    if(!ctx->typeRef().empty()){
        throw std::runtime_error{"as casts are not supported yet"};
    }
    return buildConditionBreakUnary(ctx->conditionBreakUnaryExpression());
}

ast::ExprPtr ASTBuilder::buildConditionBreakClosedAdditive(rx::Parser::ConditionBreakClosedAdditiveExpressionContext *ctx){
    if(ctx->conditionBreakClosedMultiplicativeExpression() != nullptr){
        return buildConditionBreakClosedMultiplicative(ctx->conditionBreakClosedMultiplicativeExpression());
    }

    auto operands = ctx->conditionMultiplicativeExpression();
    return buildBinaryChain(
        buildConditionBreakMultiplicative(ctx->conditionBreakMultiplicativeExpression()),
        ctx->additiveOperator(),
        [&](std::size_t i){
            if(i < operands.size()){
                return buildConditionMultiplicative(operands[i]);
            }
            // 最后一项保留 closed 入口，供外层的 < 正确解析。
            return buildConditionClosedMultiplicative(ctx->conditionClosedMultiplicativeExpression());
        }
    );
}

ast::ExprPtr ASTBuilder::buildConditionBreakClosedMultiplicative(rx::Parser::ConditionBreakClosedMultiplicativeExpressionContext *ctx){
    if(ctx->conditionBreakClosedCastExpression() != nullptr){
        return buildConditionBreakClosedCast(ctx->conditionBreakClosedCastExpression());
    }

    auto operands = ctx->conditionCastExpression();
    return buildBinaryChain(
        buildConditionBreakCast(ctx->conditionBreakCastExpression()),
        ctx->multiplicativeOperator(),
        [&](std::size_t i){
            if(i < operands.size()){
                return buildConditionCast(operands[i]);
            }
            // 最后一项保留 closed 入口，供外层的 < 正确解析。
            return buildConditionClosedCast(ctx->conditionClosedCastExpression());
        }
    );
}

ast::ExprPtr ASTBuilder::buildConditionBreakClosedCast(rx::Parser::ConditionBreakClosedCastExpressionContext *ctx){
    if(ctx->conditionBreakUnaryExpression() == nullptr){
        throw std::runtime_error{"as casts are not supported yet"};
    }
    return buildConditionBreakUnary(ctx->conditionBreakUnaryExpression());
}

ast::ExprPtr ASTBuilder::buildConditionBreakUnary(rx::Parser::ConditionBreakUnaryExpressionContext *ctx){
    if(ctx->unaryOperator() != nullptr){
        std::string op = ctx->unaryOperator()->getText();
        if(op != "!" && op != "-"){
            throw std::runtime_error{"this unary operator is not supported yet"};
        }

        // 前缀运算符后的操作数使用普通条件规则，例如 break -{ 1 }。
        auto operand = buildConditionUnary(ctx->conditionUnaryExpression());
        return std::make_unique<ast::UnaryExpr>(
            std::move(op),
            std::move(operand)
        );
    }
    return buildConditionBreakPostfix(ctx->conditionBreakPostfixExpression());
}

ast::ExprPtr ASTBuilder::buildConditionBreakPostfix(rx::Parser::ConditionBreakPostfixExpressionContext *ctx){
    auto result = buildConditionPrimaryWithoutBareBlock(ctx->conditionPrimaryWithoutBareBlock());
    for (auto *suffix : ctx->postfixSuffix()) {
        result = buildPostfixSuffix(std::move(result), suffix);
    }
    return result;
}

ast::ExprPtr ASTBuilder::buildIf(rx::Parser::IfExpressionContext *ctx){
    auto condition = buildConditionExpression(ctx->conditionExpression());
    auto thenBranch = buildBlock(ctx->blockExpression(0));
    ast::ExprPtr elseBranch;

    if(ctx->ifExpression() != nullptr){
        elseBranch = buildIf(ctx->ifExpression());
    }else if(ctx->blockExpression().size() == 2){
        elseBranch = buildBlock(ctx->blockExpression(1));
    }

    return std::make_unique<ast::IfExpr>(
        std::move(condition),
        std::move(thenBranch),
        std::move(elseBranch)
    );
}

ast::ExprPtr ASTBuilder::buildLoop(rx::Parser::BlockExpressionContext *ctx){
    // 复用块构建，保留 break 的可选值、continue 和嵌套循环等子节点。
    auto block = buildBlock(ctx);

    return std::make_unique<ast::LoopExpr>(
        std::move(block)
    );
}

ast::ExprPtr ASTBuilder::buildWhile(rx::Parser::ConditionExpressionContext *conditionCtx, rx::Parser::BlockExpressionContext *blockCtx){
    // 与 if 共用条件表达式入口，保留比较、逻辑运算等表达式的优先级。
    auto condition = buildConditionExpression(conditionCtx);
    // 递归构建循环体中的语句和尾表达式，也支持嵌套 while。
    auto block = buildBlock(blockCtx);

    // 将两个子节点的所有权交给 WhileExpr。
    return std::make_unique<ast::WhileExpr>(
        std::move(condition),
        std::move(block)
    );
}

ast::ExprPtr ASTBuilder::buildExpressionWithBlock(rx::Parser::ExpressionWithBlockContext *ctx){
    //判断 WHILE、LOOP 必须放在普通块判断前面
    // 循环分支也包含 blockExpression，先判断普通块会丢失循环信息。
    if(ctx->WHILE() != nullptr){
        return buildWhile(ctx->conditionExpression(), ctx->blockExpression());
    }
    if(ctx->LOOP() != nullptr){
        return buildLoop(ctx->blockExpression());
    }
    if(ctx->ifExpression() != nullptr){
        return buildIf(ctx->ifExpression());
    }
    if(ctx->blockExpression() != nullptr){
        return buildBlock(ctx->blockExpression());
    }
    throw std::runtime_error{"this expression with block is not supported yet"};
}

std::unique_ptr<ast::FunctionParam> ASTBuilder::buildNamedParam(rx::Parser::FunctionParamContext *ctx){
    auto binding = ctx->identifierBinding();

    std::string name = binding->identifier()->getText();
    bool isMutable = binding->MUT() != nullptr;
    auto type = buildTypeRef(ctx->typeRef());

    return std::make_unique<ast::NamedFunctionParam>(
        std::move(name),
        isMutable,
        std::move(type)
    );
}

std::unique_ptr<ast::TypeRef> ASTBuilder::buildTypeRef(rx::Parser::TypeRefContext *ctx){
    if(ctx->typePath() != nullptr){
        return std::make_unique<ast::SimpleTypeRef>(
            ctx->typePath()->getText()
        );
    }

    if (ctx->LPAREN() != nullptr && ctx->typeRef() == nullptr) {
        return std::make_unique<ast::SimpleTypeRef>("()");
    }

    throw std::runtime_error("this type form is not supported yet");
}

std::unique_ptr<ast::SelfFunctionParam> ASTBuilder::buildSelfParam(rx::Parser::SelfParamContext *ctx) {
    bool isReference = ctx->AMP() != nullptr;
    bool isMutable = ctx->MUT() != nullptr;

    std::optional<std::string> lifetime;

    if (ctx->lifetime() != nullptr) {
        lifetime = ctx->lifetime()->getText();
    }

    return std::make_unique<ast::SelfFunctionParam>(
        isReference,
        isMutable,
        std::move(lifetime)
    );
}

std::unique_ptr<ast::FunctionParam> ASTBuilder::buildFunctionParam(rx::Parser::FunctionParamContext *ctx){
    auto bindings = ctx->identifierBinding();
    auto identifier = bindings->identifier();

    std::string name = identifier->getText();
    bool isMutable = bindings->MUT();
    auto type = buildTypeRef(ctx->typeRef());

    return std::make_unique<ast::NamedFunctionParam>(
        std::move(name),
        isMutable,
        std::move(type)
    );
}
}
