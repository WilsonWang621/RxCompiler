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

    throw std::runtime_error(
        "minimal AST currently supports only function items"
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
        auto withBlock = ctx->expressionWithBlock();
        // while 是表达式；出现在语句位置时，外层包装为 ExprStmt。
        if(withBlock->WHILE() != nullptr){
            auto whileExpression = buildWhile(withBlock->conditionExpression(), withBlock->blockExpression());
            return std::make_unique<ast::ExprStmt>(
                std::move(whileExpression)
            );
        }
        // loop 与 while 一样，在语句位置包装为 ExprStmt。
        if(withBlock->LOOP() != nullptr){
            auto loopExpression = buildLoop(withBlock->blockExpression());
            return std::make_unique<ast::ExprStmt>(
                std::move(loopExpression)
            );
        }
        if(withBlock->blockExpression() != nullptr){
            auto block = buildBlock(withBlock->blockExpression());
            return std::make_unique<ast::ExprStmt>(
                std::move(block)
            );
        }
        if(withBlock->ifExpression() != nullptr){
            auto ifExpression = buildIf(withBlock->ifExpression());
            return std::make_unique<ast::ExprStmt>(
                std::move(ifExpression)
            );
        }
    }

    throw std::runtime_error("this statement form is not supported yet");
}

ast::ExprPtr ASTBuilder::buildExpression(rx::Parser::ExpressionContext *ctx){
    auto *assignment = ctx->assignmentExpression();

    auto *logicalOr = assignment->logicalOrExpression();
    auto left = buildLogicalOr(logicalOr);

    auto *op = assignment->assignmentOperator();

    // 没有赋值运算，直接返回原表达式。
    if (op == nullptr) {
        return left;
    }

    // 本次只支持 =，暂不支持 += 等复合赋值。
    if (op->equalsSign() == nullptr) {
        throw std::runtime_error(
            "compound assignment is not supported yet"
        );
    }

    // 右侧是完整 expression，递归构造。
    auto right = buildExpression(assignment->expression());

    return std::make_unique<ast::AssignExpr>(
        std::move(left),
        std::move(right)
    );
}

ast::ExprPtr ASTBuilder::buildLiteral(rx::Parser::LiteralExpressionContext *ctx){
    if(ctx->TRUE() != nullptr || ctx->FALSE() != nullptr){
        bool flag = ctx->TRUE() != nullptr;

        return std::make_unique<ast::BooleanLiteralExpr>(flag);
    }

    auto integer = ctx->INTEGER_LITERAL();
    if(integer != nullptr){
        std::string text = integer->getText();

        // 本次仅支持由十进制数字组成的字面量。
        // 暂不处理进制前缀、下划线和类型后缀。
        if (text.empty() || text.find_first_not_of("0123456789") != std::string::npos) {
            throw std::runtime_error(
                "only unsuffixed decimal integer literals are supported for now"
            );
        }

        return std::make_unique<ast::IntegerLiteralExpr>(
            std::move(text)
        );
    }

    throw std::runtime_error(
        "only integer and boolean literals are supported for now"
    );
}

ast::ExprPtr ASTBuilder::buildAdditive(rx::Parser::AdditiveExpressionContext *ctx){
    auto operands = ctx->multiplicativeExpression();
    auto operators = ctx->additiveOperator();

    // 先构造第一个操作数。
    auto result = buildMultiplicative(operands[0]);

    // 从左到右，逐次把已有结果作为新的左孩子。
    for (std::size_t i = 0; i < operators.size(); ++i) {
        std::string op = operators[i]->getText();
        auto right = buildMultiplicative(operands[i + 1]);

        result = std::make_unique<ast::BinaryExpr>(
            std::move(op),
            std::move(result),
            std::move(right)
        );
    }

    return result;
}

ast::ExprPtr ASTBuilder::buildMultiplicative(rx::Parser::MultiplicativeExpressionContext *ctx) {
    auto operands = ctx->castExpression();
    auto operators = ctx->multiplicativeOperator();

    auto result = buildCast(operands[0]);

    for (std::size_t i = 0; i < operators.size(); ++i) {
        std::string op = operators[i]->getText();
        auto right = buildCast(operands[i + 1]);

        result = std::make_unique<ast::BinaryExpr>(
            std::move(op),
            std::move(result),
            std::move(right)
        );
    }

    return result;
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
    if(!ctx->postfixSuffix().empty()){
        throw std::runtime_error{"postfix operations are not supported yet"};
    }

    return buildPrimary(ctx->primaryExpression());
}

ast::ExprPtr ASTBuilder::buildPrimary(rx::Parser::PrimaryExpressionContext *ctx) {
    auto nonBlock = ctx->nonBlockPrimary();

    if (nonBlock != nullptr) {
        return buildNonBlockPrimary(nonBlock);
    }

    auto withBlock = ctx->expressionWithBlock();

    if (withBlock != nullptr) {
        // 普通表达式入口，例如 let x = while false {};。
        if(withBlock->WHILE() != nullptr){
            return buildWhile(withBlock->conditionExpression(), withBlock->blockExpression());
        }
        if(withBlock->LOOP() != nullptr){
            return buildLoop(withBlock->blockExpression());
        }

        auto block = withBlock->blockExpression();

        if (block != nullptr) {
            return buildBlock(block);
        }

        auto ifExpression = withBlock->ifExpression();
        if(ifExpression != nullptr){
            return buildIf(ifExpression);
        }
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
    auto *assignment = ctx->statementAssignmentExpression();

    auto *logicalOr = assignment->statementLogicalOrExpression();
    auto left = buildStatementLogicalOr(logicalOr);

    auto *op = assignment->assignmentOperator();

    if (op == nullptr) {
        return left;
    }

    if (op->equalsSign() == nullptr) {
        throw std::runtime_error(
            "compound assignment is not supported yet"
        );
    }

    // 注意：右侧回到普通 expression 入口
    auto right = buildExpression(assignment->expression());

    return std::make_unique<ast::AssignExpr>(
        std::move(left),
        std::move(right)
    );
}

ast::ExprPtr ASTBuilder::buildStatementAdditive(rx::Parser::StatementAdditiveExpressionContext *ctx) {
    auto result = buildStatementMultiplicative(ctx->statementMultiplicativeExpression());

    auto operators = ctx->additiveOperator();
    auto operands = ctx->multiplicativeExpression();

    for (std::size_t i = 0; i < operators.size(); ++i) {
        std::string op = operators[i]->getText();
        auto right = buildMultiplicative(operands[i]);

        result = std::make_unique<ast::BinaryExpr>(
            std::move(op),
            std::move(result),
            std::move(right)
        );
    }

    return result;
}

ast::ExprPtr ASTBuilder::buildStatementMultiplicative(rx::Parser::StatementMultiplicativeExpressionContext *ctx) {
    auto result = buildStatementCast(ctx->statementCastExpression());

    auto operators = ctx->multiplicativeOperator();
    auto operands = ctx->castExpression();

    for (std::size_t i = 0; i < operators.size(); ++i) {
        std::string op = operators[i]->getText();
        auto right = buildCast(operands[i]);

        result = std::make_unique<ast::BinaryExpr>(
            std::move(op),
            std::move(result),
            std::move(right)
        );
    }

    return result;
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
        ctx->dotSuffix() != nullptr ||
        !ctx->postfixSuffix().empty()) {
        throw std::runtime_error(
            "block-leading or postfix expressions are not supported yet"
        );
    }

    return buildNonBlockPrimary(ctx->nonBlockPrimary());
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
    
    auto left = buildConditionLogicalOr(assignment->conditionLogicalOrExpression());
    auto op = assignment->assignmentOperator();

    if(op == nullptr){
        return left;
    }
    if(op->equalsSign() == nullptr){
        throw std::runtime_error{"compound assignment is not supported yet"};
    }

    auto right = buildConditionExpression(assignment->conditionExpression());

    return std::make_unique<ast::AssignExpr>(
        std::move(left),
        std::move(right)
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
    auto result = buildLogicalAnd(operands[0]);

    for(std::size_t i = 1; i < operands.size(); ++i){
        auto right = buildLogicalAnd(operands[i]);
        result = std::make_unique<ast::BinaryExpr>(
            "||",
            std::move(result),
            std::move(right)
        );
    }
    return result;
}

ast::ExprPtr ASTBuilder::buildLogicalAnd(rx::Parser::LogicalAndExpressionContext *ctx){
    auto operands = ctx->comparisonExpression();
    auto result = buildComparison(operands[0]);

    for(std::size_t i = 1; i < operands.size(); ++i){
        auto right = buildComparison(operands[i]);
        result = std::make_unique<ast::BinaryExpr>(
            "&&",
            std::move(result),
            std::move(right)
        );
    }
    return result;
}

ast::ExprPtr ASTBuilder::buildComparison(rx::Parser::ComparisonExpressionContext *ctx){
    //closedBitOrExpression LT bitOrExpression
    if(ctx->LT() != nullptr){
        auto left = buildClosedBitOr(ctx->closedBitOrExpression());
        auto right = buildBitOr(ctx->bitOrExpression(0));
        
        return std::make_unique<ast::BinaryExpr>(
            "<",
            std::move(left),
            std::move(right)
        );
    }

    //bitOrExpression (comparisonExceptLt bitOrExpression)?
    auto left = buildBitOr(ctx->bitOrExpression(0));
    if(ctx->comparisonExceptLt() == nullptr){
        return left;
    }
    std::string op = ctx->comparisonExceptLt()->getText();
    auto right = buildBitOr(ctx->bitOrExpression(1));

    return std::make_unique<ast::BinaryExpr>(
        op,
        std::move(left),
        std::move(right)
    );
}

ast::ExprPtr ASTBuilder::buildClosedAdditive(rx::Parser::ClosedAdditiveExpressionContext *ctx){
    auto operands = ctx->multiplicativeExpression();
    auto operators = ctx->additiveOperator();

    if(operands.empty()){
        return buildClosedMultiplicative(ctx->closedMultiplicativeExpression());
    }
    
    auto result = buildMultiplicative(operands[0]);
    for(size_t i = 0; i < operators.size(); i++){
        std::string op = operators[i]->getText();
        ast::ExprPtr right;
        if(i + 1 < operands.size()){
            right = buildMultiplicative(operands[i + 1]);
        }else{
            right = buildClosedMultiplicative(ctx->closedMultiplicativeExpression());
        }
        result = std::make_unique<ast::BinaryExpr>(
            std::move(op),
            std::move(result),
            std::move(right)
        );
    }
    return result;
}

ast::ExprPtr ASTBuilder::buildClosedMultiplicative(rx::Parser::ClosedMultiplicativeExpressionContext *ctx){
    auto operands = ctx->castExpression();
    auto operators = ctx->multiplicativeOperator();

    if(operands.empty()){
        return buildClosedCast(ctx->closedCastExpression());
    }

    auto result = buildCast(operands[0]);
    for(size_t i = 0; i < operators.size(); i++){
        std::string op = operators[i]->getText();
        ast::ExprPtr right;
        if(i + 1 < operands.size()){
            right = buildCast(operands[i + 1]);
        }
        else{
            right = buildClosedCast(ctx->closedCastExpression());
        }

        result = std::make_unique<ast::BinaryExpr>(
            std::move(op),
            std::move(result),
            std::move(right)
        );
    }
    return result;
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
    auto result = buildStatementLogicalAnd(ctx->statementLogicalAndExpression());

    for(std::size_t i = 0; i < operands.size(); ++i){
        auto right = buildLogicalAnd(operands[i]);
        result = std::make_unique<ast::BinaryExpr>(
            "||",
            std::move(result),
            std::move(right)
        );
    }
    return result;
}

ast::ExprPtr ASTBuilder::buildStatementLogicalAnd(rx::Parser::StatementLogicalAndExpressionContext *ctx){
    auto operands = ctx->comparisonExpression();
    auto result = buildStatementComparison(ctx->statementComparisonExpression());

    for(std::size_t i = 0; i < operands.size(); ++i){
        auto right = buildComparison(operands[i]);
        result = std::make_unique<ast::BinaryExpr>(
            "&&",
            std::move(result),
            std::move(right)
        );
    }
    return result;
}

ast::ExprPtr ASTBuilder::buildStatementComparison(rx::Parser::StatementComparisonExpressionContext *ctx){
    if(ctx->LT() != nullptr){
        auto left = buildStatementClosedBitOr(ctx->statementClosedBitOrExpression());
        auto right = buildBitOr(ctx->bitOrExpression());

        return std::make_unique<ast::BinaryExpr>(
            "<",
            std::move(left),
            std::move(right)
        );
    }

    auto left = buildStatementBitOr(ctx->statementBitOrExpression());
    auto *op = ctx->comparisonExceptLt();
    if(op == nullptr){
        return left;
    }

    auto right = buildBitOr(ctx->bitOrExpression());
    return std::make_unique<ast::BinaryExpr>(
        op->getText(),
        std::move(left),
        std::move(right)
    );

}

ast::ExprPtr ASTBuilder::buildStatementClosedAdditive(rx::Parser::StatementClosedAdditiveExpressionContext *ctx){
    if(ctx->statementClosedMultiplicativeExpression() != nullptr){
        return buildStatementClosedMultiplicative(ctx->statementClosedMultiplicativeExpression());
    }

    auto result = buildStatementMultiplicative(ctx->statementMultiplicativeExpression());
    auto operands = ctx->multiplicativeExpression();
    auto operators = ctx->additiveOperator();
    for(std::size_t i = 0; i < operators.size(); ++i){
        std::string op = operators[i]->getText();
        ast::ExprPtr right;
        if(i < operands.size()){
            right = buildMultiplicative(operands[i]);
        }else{
            right = buildClosedMultiplicative(ctx->closedMultiplicativeExpression());
        }
        result = std::make_unique<ast::BinaryExpr>(
            std::move(op),
            std::move(result),
            std::move(right)
        );
    }
    return result;
}

ast::ExprPtr ASTBuilder::buildStatementClosedMultiplicative(rx::Parser::StatementClosedMultiplicativeExpressionContext *ctx){
    if(ctx->statementClosedCastExpression() != nullptr){
        return buildStatementClosedCast(ctx->statementClosedCastExpression());
    }

    auto result = buildStatementCast(ctx->statementCastExpression());
    auto operands = ctx->castExpression();
    auto operators = ctx->multiplicativeOperator();
    for(std::size_t i = 0; i < operators.size(); ++i){
        std::string op = operators[i]->getText();
        ast::ExprPtr right;
        if(i < operands.size()){
            right = buildCast(operands[i]);
        }else{
            right = buildClosedCast(ctx->closedCastExpression());
        }
        result = std::make_unique<ast::BinaryExpr>(
            std::move(op),
            std::move(result),
            std::move(right)
        );
    }
    return result;
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
    auto operators = ctx->additiveOperator();
    auto result = buildConditionMultiplicative(operands[0]);

    for (std::size_t i = 0; i < operators.size(); ++i) {
        auto right = buildConditionMultiplicative(operands[i + 1]);
        result = std::make_unique<ast::BinaryExpr>(
            operators[i]->getText(),
            std::move(result),
            std::move(right)
        );
    }
    return result;
}

ast::ExprPtr ASTBuilder::buildConditionMultiplicative(rx::Parser::ConditionMultiplicativeExpressionContext *ctx){
    auto operands = ctx->conditionCastExpression();
    auto operators = ctx->multiplicativeOperator();
    auto result = buildConditionCast(operands[0]);

    for (std::size_t i = 0; i < operators.size(); ++i) {
        auto right = buildConditionCast(operands[i + 1]);
        result = std::make_unique<ast::BinaryExpr>(
            operators[i]->getText(),
            std::move(result),
            std::move(right)
        );
    }
    return result;
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
    auto result = buildConditionLogicalAnd(operands[0]);

    for(std::size_t i = 1; i < operands.size(); ++i){
        auto right = buildConditionLogicalAnd(operands[i]);
        result = std::make_unique<ast::BinaryExpr>(
            "||",
            std::move(result),
            std::move(right)
        );
    }
    return result;
}

ast::ExprPtr ASTBuilder::buildConditionLogicalAnd(rx::Parser::ConditionLogicalAndExpressionContext *ctx){
    auto operands = ctx->conditionComparisonExpression();
    auto result = buildConditionComparison(operands[0]);

    for(std::size_t i = 1; i < operands.size(); ++i){
        auto right = buildConditionComparison(operands[i]);
        result = std::make_unique<ast::BinaryExpr>(
            "&&",
            std::move(result),
            std::move(right)
        );
    }
    return result;
}


ast::ExprPtr ASTBuilder::buildConditionComparison(rx::Parser::ConditionComparisonExpressionContext *ctx){
    if(ctx->LT() != nullptr){
        auto left = buildConditionClosedBitOr(ctx->conditionClosedBitOrExpression());
        auto right = buildConditionBitOr(ctx->conditionBitOrExpression(0));
        return std::make_unique<ast::BinaryExpr>(
            "<",
            std::move(left),
            std::move(right)
        );
    }

    auto left = buildConditionBitOr(ctx->conditionBitOrExpression(0));
    auto *op = ctx->comparisonExceptLt();
    if(op == nullptr){
        return left;
    }

    auto right = buildConditionBitOr(ctx->conditionBitOrExpression(1));
    return std::make_unique<ast::BinaryExpr>(
        op->getText(),
        std::move(left),
        std::move(right)
    );
}

ast::ExprPtr ASTBuilder::buildConditionClosedAdditive(rx::Parser::ConditionClosedAdditiveExpressionContext *ctx){
    auto operands = ctx->conditionMultiplicativeExpression();
    auto operators = ctx->additiveOperator();
    if(operands.empty()){
        return buildConditionClosedMultiplicative(ctx->conditionClosedMultiplicativeExpression());
    }

    auto result = buildConditionMultiplicative(operands[0]);
    for(std::size_t i = 0; i < operators.size(); ++i){
        std::string op = operators[i]->getText();
        ast::ExprPtr right;
        if(i + 1 < operands.size()){
            right = buildConditionMultiplicative(operands[i + 1]);
        }else{
            right = buildConditionClosedMultiplicative(ctx->conditionClosedMultiplicativeExpression());
        }
        result = std::make_unique<ast::BinaryExpr>(
            std::move(op),
            std::move(result),
            std::move(right)
        );
    }
    return result;
}

ast::ExprPtr ASTBuilder::buildConditionClosedMultiplicative(rx::Parser::ConditionClosedMultiplicativeExpressionContext *ctx){
    auto operands = ctx->conditionCastExpression();
    auto operators = ctx->multiplicativeOperator();
    if(operands.empty()){
        return buildConditionClosedCast(ctx->conditionClosedCastExpression());
    }

    auto result = buildConditionCast(operands[0]);
    for(std::size_t i = 0; i < operators.size(); ++i){
        std::string op = operators[i]->getText();
        ast::ExprPtr right;
        if(i + 1 < operands.size()){
            right = buildConditionCast(operands[i + 1]);
        }else{
            right = buildConditionClosedCast(ctx->conditionClosedCastExpression());
        }
        result = std::make_unique<ast::BinaryExpr>(
            std::move(op),
            std::move(result),
            std::move(right)
        );
    }
    return result;
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
    if(!ctx->postfixSuffix().empty()){
        throw std::runtime_error{"postfix operations are not supported yet"};
    }
    return buildConditionPrimary(ctx->conditionPrimary());
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
    auto left = buildConditionBreakLogicalOr(assignment->conditionBreakLogicalOrExpression());
    auto op = assignment->assignmentOperator();

    if(op == nullptr){
        return left;
    }
    if(op->equalsSign() == nullptr){
        throw std::runtime_error{"compound assignment is not supported yet"};
    }

    // 只有首个操作数使用 conditionBreak 规则，后续操作数回到普通条件入口。
    auto right = buildConditionExpression(assignment->conditionExpression());
    return std::make_unique<ast::AssignExpr>(
        std::move(left),
        std::move(right)
    );
}

ast::ExprPtr ASTBuilder::buildConditionBreakLogicalOr(rx::Parser::ConditionBreakLogicalOrExpressionContext *ctx){
    auto operands = ctx->conditionLogicalAndExpression();
    auto result = buildConditionBreakLogicalAnd(ctx->conditionBreakLogicalAndExpression());

    for(std::size_t i = 0; i < operands.size(); ++i){
        auto right = buildConditionLogicalAnd(operands[i]);
        result = std::make_unique<ast::BinaryExpr>(
            "||",
            std::move(result),
            std::move(right)
        );
    }
    return result;
}

ast::ExprPtr ASTBuilder::buildConditionBreakLogicalAnd(rx::Parser::ConditionBreakLogicalAndExpressionContext *ctx){
    auto operands = ctx->conditionComparisonExpression();
    auto result = buildConditionBreakComparison(ctx->conditionBreakComparisonExpression());

    for(std::size_t i = 0; i < operands.size(); ++i){
        auto right = buildConditionComparison(operands[i]);
        result = std::make_unique<ast::BinaryExpr>(
            "&&",
            std::move(result),
            std::move(right)
        );
    }
    return result;
}

ast::ExprPtr ASTBuilder::buildConditionBreakComparison(rx::Parser::ConditionBreakComparisonExpressionContext *ctx){
    if(ctx->LT() != nullptr){
        auto left = buildConditionBreakClosedBitOr(ctx->conditionBreakClosedBitOrExpression());
        auto right = buildConditionBitOr(ctx->conditionBitOrExpression());
        return std::make_unique<ast::BinaryExpr>(
            "<",
            std::move(left),
            std::move(right)
        );
    }

    auto left = buildConditionBreakBitOr(ctx->conditionBreakBitOrExpression());
    auto *op = ctx->comparisonExceptLt();
    if(op == nullptr){
        return left;
    }

    auto right = buildConditionBitOr(ctx->conditionBitOrExpression());
    return std::make_unique<ast::BinaryExpr>(
        op->getText(),
        std::move(left),
        std::move(right)
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
    auto operators = ctx->additiveOperator();
    auto result = buildConditionBreakMultiplicative(ctx->conditionBreakMultiplicativeExpression());

    for(std::size_t i = 0; i < operators.size(); ++i){
        auto right = buildConditionMultiplicative(operands[i]);
        result = std::make_unique<ast::BinaryExpr>(
            operators[i]->getText(),
            std::move(result),
            std::move(right)
        );
    }
    return result;
}

ast::ExprPtr ASTBuilder::buildConditionBreakMultiplicative(rx::Parser::ConditionBreakMultiplicativeExpressionContext *ctx){
    auto operands = ctx->conditionCastExpression();
    auto operators = ctx->multiplicativeOperator();
    auto result = buildConditionBreakCast(ctx->conditionBreakCastExpression());

    for(std::size_t i = 0; i < operators.size(); ++i){
        auto right = buildConditionCast(operands[i]);
        result = std::make_unique<ast::BinaryExpr>(
            operators[i]->getText(),
            std::move(result),
            std::move(right)
        );
    }
    return result;
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

    auto result = buildConditionBreakMultiplicative(ctx->conditionBreakMultiplicativeExpression());
    auto operands = ctx->conditionMultiplicativeExpression();
    auto operators = ctx->additiveOperator();
    // closed 分支的最后一个操作数使用 closed 入口，供外层的 < 正确解析。
    for(std::size_t i = 0; i < operators.size(); ++i){
        ast::ExprPtr right;
        if(i < operands.size()){
            right = buildConditionMultiplicative(operands[i]);
        }else{
            right = buildConditionClosedMultiplicative(ctx->conditionClosedMultiplicativeExpression());
        }
        result = std::make_unique<ast::BinaryExpr>(
            operators[i]->getText(),
            std::move(result),
            std::move(right)
        );
    }
    return result;
}

ast::ExprPtr ASTBuilder::buildConditionBreakClosedMultiplicative(rx::Parser::ConditionBreakClosedMultiplicativeExpressionContext *ctx){
    if(ctx->conditionBreakClosedCastExpression() != nullptr){
        return buildConditionBreakClosedCast(ctx->conditionBreakClosedCastExpression());
    }

    auto result = buildConditionBreakCast(ctx->conditionBreakCastExpression());
    auto operands = ctx->conditionCastExpression();
    auto operators = ctx->multiplicativeOperator();
    for(std::size_t i = 0; i < operators.size(); ++i){
        ast::ExprPtr right;
        if(i < operands.size()){
            right = buildConditionCast(operands[i]);
        }else{
            right = buildConditionClosedCast(ctx->conditionClosedCastExpression());
        }
        result = std::make_unique<ast::BinaryExpr>(
            operators[i]->getText(),
            std::move(result),
            std::move(right)
        );
    }
    return result;
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
    if(!ctx->postfixSuffix().empty()){
        throw std::runtime_error{"postfix operations are not supported yet"};
    }
    return buildConditionPrimaryWithoutBareBlock(ctx->conditionPrimaryWithoutBareBlock());
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
        return std::make_unique<ast::TypeRef>(
            ctx->typePath()->getText()
        );
    }

    if (ctx->LPAREN() != nullptr && ctx->typeRef() == nullptr) {
        return std::make_unique<ast::TypeRef>("()");
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
