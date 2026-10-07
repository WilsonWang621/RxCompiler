#include"ast_builder.hpp"

#include <stdexcept>

namespace {

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
    auto right = buildRight();
    if (op->equalsSign() == nullptr) {
        return std::make_unique<rx::ast::CompoundAssignExpr>(
            op->getText(), std::move(left), std::move(right)
        );
    }
    return std::make_unique<rx::ast::AssignExpr>(
        std::move(left),
        std::move(right)
    );
}

rx::ast::ExprPtr buildUnaryOperator(rx::Parser::UnaryOperatorContext *ctx, rx::ast::ExprPtr operand) {
    if (ctx->AMP() != nullptr || ctx->ANDAND() != nullptr) {
        operand = std::make_unique<rx::ast::UnaryExpr>(
            ctx->MUT() != nullptr ? "&mut" : "&", std::move(operand)
        );
        // &&mut x 等价于 &(&mut x)，mut 只属于内层借用。
        if (ctx->ANDAND() != nullptr) {
            operand = std::make_unique<rx::ast::UnaryExpr>("&", std::move(operand));
        }
        return operand;
    }
    return std::make_unique<rx::ast::UnaryExpr>(ctx->getText(), std::move(operand));
}

template<class Context>
std::unique_ptr<rx::ast::TypeRef> buildReferenceType(Context *ctx, std::unique_ptr<rx::ast::TypeRef> referent) {
    // lifetime 已通过语法检查，在 AST 阶段丢弃；保留引用层数和可变性。
    referent = std::make_unique<rx::ast::ReferenceTypeRef>(
        std::move(referent), ctx->MUT() != nullptr
    );
    // 类型中的 && 同样表示两层引用，生命周期与 mut 只属于内层。
    if (ctx->ANDAND() != nullptr) {
        referent = std::make_unique<rx::ast::ReferenceTypeRef>(std::move(referent), false);
    }
    return referent;
}

} // namespace

namespace rx::frontend{

std::unique_ptr<ast::Crate> ASTBuilder::build(rx::Parser::CrateContext *ctx){
    auto result = std::make_unique<ast::Crate>();
    for(auto itemCtx : ctx->item()){
        // 课程规范允许解析后丢弃整条 use，不产生空的 Item 节点。
        if (itemCtx->useDeclaration() != nullptr) {
            continue;
        }
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

    if (ctx->structDefinition() != nullptr) {
        return buildStructItem(ctx->structDefinition());
    }

    if (ctx->inherentImpl() != nullptr) {
        return buildInherentImpl(ctx->inherentImpl());
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

std::unique_ptr<ast::StructItem> ASTBuilder::buildStructItem(rx::Parser::StructDefinitionContext *ctx) {

    std::vector<std::unique_ptr<ast::OuterAttribute>> attributes;
    for (auto *attributeCtx : ctx->outerAttribute()) {
        attributes.push_back(buildOuterAttribute(attributeCtx));
    }


    std::vector<std::unique_ptr<ast::StructField>> fields;
    for (auto *fieldCtx : ctx->structField()) {
        fields.push_back(buildStructField(fieldCtx));
    }

    return std::make_unique<ast::StructItem>(
        ctx->identifier()->getText(), std::move(fields), std::move(attributes)
    );
}

std::unique_ptr<ast::StructField> ASTBuilder::buildStructField(rx::Parser::StructFieldContext *ctx) {
    auto type = buildTypeRef(ctx->typeRef());
    return std::make_unique<ast::StructField>(ctx->identifier()->getText(), std::move(type));
}

std::unique_ptr<ast::ImplItem> ASTBuilder::buildInherentImpl(rx::Parser::InherentImplContext *ctx) {
    auto type = buildTypeRef(ctx->typeRef());
    std::vector<ast::ItemPtr> items;
    for (auto *itemCtx : ctx->associatedItem()) {
        if (itemCtx->constantItem() != nullptr) {
            items.push_back(buildConstItem(itemCtx->constantItem()));
        } else {
            items.push_back(buildFunction(itemCtx->functionDefinition()));
        }
    }
    return std::make_unique<ast::ImplItem>(
        std::move(type), std::move(items)
    );
}

std::unique_ptr<ast::OuterAttribute> ASTBuilder::buildOuterAttribute(rx::Parser::OuterAttributeContext *ctx) {
    // 当前文法仅允许 derive 属性；是否能派生这些能力留给语义分析。
    std::vector<std::string> names;
    for (auto *nameCtx : ctx->deriveName()) {
        names.push_back(nameCtx->getText());
    }
    return std::make_unique<ast::DeriveAttribute>(std::move(names));
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
        std::move(body),
        ctx->genericParams() != nullptr && !ctx->genericParams()->lifetimeParam().empty()
    );
}

std::unique_ptr<ast::BlockExpr> ASTBuilder::buildBlock(rx::Parser::BlockExpressionContext *ctx){
    auto block = std::make_unique<ast::BlockExpr>();

    const auto statements = ctx->statement();
    for (std::size_t i = 0; i < statements.size(); ++i) {
        auto *statementCtx = statements[i];
        // 文法把带块表达式也解析为 statement；末尾且没有分号时实际是尾表达式。
        if (i + 1 == statements.size() && ctx->statementExpression() == nullptr &&
            statementCtx->expressionWithBlock() != nullptr && statementCtx->SEMI() == nullptr) {
            block->setTail(buildExpressionWithBlock(statementCtx->expressionWithBlock()));
        } else {
            block->addStatement(buildStatement(statementCtx));
        }
    }
    if( ctx->statementExpression() != nullptr){
        block->setTail(buildStatementExpression(ctx->statementExpression()));
    }
    

    return block;
}

std::unique_ptr<ast::LetStmt> ASTBuilder::buildLet(rx::Parser::LetStatementContext *ctx){
    auto *binding = ctx->identifierBinding();

    // 注解和初始化值分别构建；二者的类型是否匹配由语义分析检查。
    std::unique_ptr<ast::TypeRef> type;
    if (auto *typeCtx = ctx->typeRef()) {
        type = buildTypeRef(typeCtx);
    }

    std::string name = binding->identifier()->getText();
    bool isMutable = binding->MUT() != nullptr;
    auto initializer = buildExpression(ctx->expression());

    return std::make_unique<ast::LetStmt>(
        std::move(name), isMutable, std::move(initializer), std::move(type)
    );
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
        return std::make_unique<ast::ExprStmt>(std::move(expression), ctx->SEMI() != nullptr);
    }

    // 其他语句也可能带分号，先处理它们，再识别单独的空语句。
    if (ctx->SEMI() != nullptr) {
        return std::make_unique<ast::EmptyStmt>();
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
        // [value; count] 保存一个元素值和次数，AST 阶段不展开重复元素。
        // 元素走完整表达式入口，嵌套数组会再次进入 buildArray。
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

    if(ctx->pathInExpression() != nullptr){
        // N 保留为路径节点，这里不查符号表或替换成整数。
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
    // 负号由 buildConstValue 包装；这里只构建负号后的操作数。
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
    return buildCastChain(buildUnary(ctx->unaryExpression()), ctx->typeRef());
}

ast::ExprPtr ASTBuilder::buildCastChain(ast::ExprPtr value, const std::vector<rx::Parser::TypeRefContext*> &types) {
    for (auto *type : types) {
        value = std::make_unique<ast::CastExpr>(std::move(value), buildTypeRef(type));
    }
    return value;
}

ast::ExprPtr ASTBuilder::buildUnary(rx::Parser::UnaryExpressionContext *ctx){
    if (ctx->unaryOperator() != nullptr) {
        // 递归先构建内层，使前缀运算从右向左嵌套。
        auto operand = buildUnary(ctx->unaryExpression());
        return buildUnaryOperator(ctx->unaryOperator(), std::move(operand));
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
    if (auto *call = ctx->callArguments()) {
        auto arguments = buildCallArguments(call);
        // base 是此前完整的表达式；是否可调用及参数类型留给语义分析检查。
        return std::make_unique<ast::CallExpr>(std::move(base), std::move(arguments));
    }

    if(ctx->LBRACKET() != nullptr){
        // base 是前一个表达式的结果，expression 是 [] 内的下标。
        auto index = buildExpression(ctx->expression());
        return std::make_unique<ast::IndexExpr>(
            std::move(base),
            std::move(index)
        );
    }

    if (auto *dot = ctx->dotSuffix()) {
        return buildDotSuffix(std::move(base), dot);
    }

    throw std::runtime_error{"this post suffix is not supported yet"};
}

ast::ExprPtr ASTBuilder::buildDotSuffix(ast::ExprPtr base, rx::Parser::DotSuffixContext *ctx) {
    if (auto *call = ctx->callArguments()) {
        auto method = buildPathSegment(ctx->pathExprSegment());
        auto arguments = buildCallArguments(call);
        // receiver 单独保存，自动借用、解引用和方法查找留给语义分析。
        return std::make_unique<ast::MethodCallExpr>(
            std::move(base), std::move(method), std::move(arguments)
        );
    }
    return std::make_unique<ast::FieldExpr>(
        std::move(base), ctx->identifier()->getText()
    );
}

std::vector<ast::ExprPtr> ASTBuilder::buildCallArguments(rx::Parser::CallArgumentsContext *ctx) {
    std::vector<ast::ExprPtr> arguments;
    // 即使调用位于条件中，括号内的参数也使用普通 expression 规则。
    for (auto *argument : ctx->expression()) {
        arguments.push_back(buildExpression(argument));
    }
    return arguments;
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

std::unique_ptr<ast::PathExpr> ASTBuilder::buildPath(rx::Parser::PathInExpressionContext *ctx){
    auto segmentContexts = ctx->pathExprSegment();

    std::vector<std::unique_ptr<ast::PathSegment>> segments;
    segments.reserve(segmentContexts.size());
    // 按源码顺序保存每一段，具体名称解析留给语义分析。
    for (auto *segmentCtx : segmentContexts) {
        segments.push_back(buildPathSegment(segmentCtx));
    }

    return std::make_unique<ast::PathExpr>(
        std::move(segments)
    );
}

std::unique_ptr<ast::StructExpr> ASTBuilder::buildStructExpr(rx::Parser::PathInExpressionContext *pathCtx,
                                                         rx::Parser::StructExprFieldsContext *fieldsCtx) {
    auto path = buildPath(pathCtx);
    std::vector<std::unique_ptr<ast::StructExprField>> fields;
    // S {} 的字段上下文为空，但仍然是构造表达式，不能退化为路径。
    if (fieldsCtx != nullptr) {
        for (auto *fieldCtx : fieldsCtx->structExprField()) {
            fields.push_back(buildStructExprField(fieldCtx));
        }
    }
    return std::make_unique<ast::StructExpr>(std::move(path), std::move(fields));
}

std::unique_ptr<ast::StructExprField> ASTBuilder::buildStructExprField(rx::Parser::StructExprFieldContext *ctx) {
    auto value = buildExpression(ctx->expression());
    return std::make_unique<ast::StructExprField>(ctx->identifier()->getText(), std::move(value));
}

std::unique_ptr<ast::PathSegment> ASTBuilder::buildPathSegment(rx::Parser::PathExprSegmentContext *ctx) {
    auto genericArgs = buildGenericArgs(ctx->genericArgs());
    // 保留普通标识符、self 和 Self；其使用上下文由语义分析检查。
    return std::make_unique<ast::PathSegment>(
        ctx->pathIdentSegment()->getText(), std::move(genericArgs)
    );
}

std::optional<ast::GenericArgs> ASTBuilder::buildGenericArgs(rx::Parser::GenericArgsContext *ctx) {
    if (ctx == nullptr) {
        return std::nullopt;
    }
    const auto argumentContexts = ctx->genericArg();
    ast::GenericArgs arguments;
    for (auto *argumentCtx : argumentContexts) {
        if (argumentCtx->typeRef() != nullptr) {
            arguments.push_back(buildTypeRef(argumentCtx->typeRef()));
        }
    }
    // 显式 <> 仍可参与具体类型实参的合法性检查；仅含 lifetime 则与省略等价。
    if (!argumentContexts.empty() && arguments.empty()) {
        return std::nullopt;
    }
    return arguments;
}

std::unique_ptr<ast::PathSegment> ASTBuilder::buildTypePathSegment(rx::Parser::TypePathSegmentContext *ctx) {
    return std::make_unique<ast::PathSegment>(
        ctx->pathIdentSegment()->getText(), buildGenericArgs(ctx->genericArgs())
    );
}

std::unique_ptr<ast::TypePathRef> ASTBuilder::buildTypePath(rx::Parser::TypePathContext *ctx) {
    std::vector<std::unique_ptr<ast::PathSegment>> segments;
    for (auto *segmentCtx : ctx->typePathSegment()) {
        segments.push_back(buildTypePathSegment(segmentCtx));
    }
    return std::make_unique<ast::TypePathRef>(std::move(segments));
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
    return buildCastChain(buildStatementUnary(ctx->statementUnaryExpression()), ctx->typeRef());
}

ast::ExprPtr ASTBuilder::buildStatementUnary(rx::Parser::StatementUnaryExpressionContext *ctx) {
    if (ctx->unaryOperator() != nullptr) {
        // 前缀运算符后的操作数使用语法规定的普通表达式规则。
        auto operand = buildUnary(ctx->unaryExpression());
        return buildUnaryOperator(ctx->unaryOperator(), std::move(operand));
    }
    return buildStatementPostfix(ctx->statementPostfixExpression());
}

ast::ExprPtr ASTBuilder::buildStatementPostfix(rx::Parser::StatementPostfixExpressionContext *ctx) {
    ast::ExprPtr result;
    if (auto *withBlock = ctx->expressionWithBlock()) {
        // 以块开头的语句先处理直接的点后缀，再处理后续调用、下标和点后缀。
        result = buildDotSuffix(buildExpressionWithBlock(withBlock), ctx->dotSuffix());
    } else {
        result = buildNonBlockPrimary(ctx->nonBlockPrimary());
    }

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
            return buildStructExpr(ctx->pathInExpression(), ctx->structExprFields());
        }

        return buildPath(ctx->pathInExpression());
    }
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
            return std::make_unique<ast::UnitExpr>();
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
    auto operands = ctx->bitXorExpression();
    return buildBinaryChain(
        buildBitXor(operands[0]),
        ctx->PIPE(),
        [&](std::size_t i){
            return buildBitXor(operands[i + 1]);
        }
    );
}

ast::ExprPtr ASTBuilder::buildBitXor(rx::Parser::BitXorExpressionContext *ctx){
    auto operands = ctx->bitAndExpression();
    return buildBinaryChain(
        buildBitAnd(operands[0]),
        ctx->CARET(),
        [&](std::size_t i){
            return buildBitAnd(operands[i + 1]);
        }
    );
}

ast::ExprPtr ASTBuilder::buildBitAnd(rx::Parser::BitAndExpressionContext *ctx){
    auto operands = ctx->shiftExpression();
    return buildBinaryChain(
        buildShift(operands[0]),
        ctx->AMP(),
        [&](std::size_t i){
            return buildShift(operands[i + 1]);
        }
    );
}

ast::ExprPtr ASTBuilder::buildShift(rx::Parser::ShiftExpressionContext *ctx){
    return buildShiftChain(ctx);
}

ast::ExprPtr ASTBuilder::buildClosedBitOr(rx::Parser::ClosedBitOrExpressionContext *ctx){
    auto operands = ctx->bitXorExpression();
    if (operands.empty()) {
        return buildClosedBitXor(ctx->closedBitXorExpression());
    }
    return buildBinaryChain(
        buildBitXor(operands[0]),
        ctx->PIPE(),
        [&](std::size_t i){
            if (i + 1 < operands.size()) {
                return buildBitXor(operands[i + 1]);
            }
            return buildClosedBitXor(ctx->closedBitXorExpression());
        }
    );
}

ast::ExprPtr ASTBuilder::buildClosedBitXor(rx::Parser::ClosedBitXorExpressionContext *ctx){
    auto operands = ctx->bitAndExpression();
    if (operands.empty()) {
        return buildClosedBitAnd(ctx->closedBitAndExpression());
    }
    return buildBinaryChain(
        buildBitAnd(operands[0]),
        ctx->CARET(),
        [&](std::size_t i){
            if (i + 1 < operands.size()) {
                return buildBitAnd(operands[i + 1]);
            }
            return buildClosedBitAnd(ctx->closedBitAndExpression());
        }
    );
}

ast::ExprPtr ASTBuilder::buildClosedBitAnd(rx::Parser::ClosedBitAndExpressionContext *ctx){
    auto operands = ctx->shiftExpression();
    if (operands.empty()) {
        return buildClosedShift(ctx->closedShiftExpression());
    }
    return buildBinaryChain(
        buildShift(operands[0]),
        ctx->AMP(),
        [&](std::size_t i){
            if (i + 1 < operands.size()) {
                return buildShift(operands[i + 1]);
            }
            return buildClosedShift(ctx->closedShiftExpression());
        }
    );
}

ast::ExprPtr ASTBuilder::buildClosedShift(rx::Parser::ClosedShiftExpressionContext *ctx){
    return buildShiftChain(ctx);
}

ast::ExprPtr ASTBuilder::buildShiftChain(antlr4::ParserRuleContext *ctx) {
    // << 前使用 closedAdditive，>> 前使用 additive；分开取两个向量会丢失交错顺序。
    // 此规则的孩子按“操作数、运算符、操作数”排列，shiftRight 的 getText() 为 >>。
    auto result = buildShiftOperand(ctx->children[0]);
    for (std::size_t i = 1; i < ctx->children.size(); i += 2) {
        auto right = buildShiftOperand(ctx->children[i + 1]);
        result = std::make_unique<ast::BinaryExpr>(
            ctx->children[i]->getText(), std::move(result), std::move(right)
        );
    }
    return result;
}

ast::ExprPtr ASTBuilder::buildShiftOperand(antlr4::tree::ParseTree *ctx) {
    if (auto *operand = dynamic_cast<rx::Parser::AdditiveExpressionContext*>(ctx)) {
        return buildAdditive(operand);
    }
    if (auto *operand = dynamic_cast<rx::Parser::ClosedAdditiveExpressionContext*>(ctx)) {
        return buildClosedAdditive(operand);
    }
    if (auto *operand = dynamic_cast<rx::Parser::StatementAdditiveExpressionContext*>(ctx)) {
        return buildStatementAdditive(operand);
    }
    if (auto *operand = dynamic_cast<rx::Parser::StatementClosedAdditiveExpressionContext*>(ctx)) {
        return buildStatementClosedAdditive(operand);
    }
    if (auto *operand = dynamic_cast<rx::Parser::ConditionAdditiveExpressionContext*>(ctx)) {
        return buildConditionAdditive(operand);
    }
    if (auto *operand = dynamic_cast<rx::Parser::ConditionClosedAdditiveExpressionContext*>(ctx)) {
        return buildConditionClosedAdditive(operand);
    }
    if (auto *operand = dynamic_cast<rx::Parser::ConditionBreakAdditiveExpressionContext*>(ctx)) {
        return buildConditionBreakAdditive(operand);
    }
    if (auto *operand = dynamic_cast<rx::Parser::ConditionBreakClosedAdditiveExpressionContext*>(ctx)) {
        return buildConditionBreakClosedAdditive(operand);
    }
    throw std::logic_error{"unexpected operand in shift expression"};
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
    if (ctx->unaryExpression() != nullptr) {
        return buildUnary(ctx->unaryExpression());
    }
    return std::make_unique<ast::CastExpr>(
        buildCast(ctx->castExpression()),
        buildClosedCastType(ctx->closedCastType())
    );
}

//statement prefix: The expression that enters from the beginning of the statement.
ast::ExprPtr ASTBuilder::buildStatementBitOr(rx::Parser::StatementBitOrExpressionContext *ctx){
    auto operands = ctx->bitXorExpression();
    return buildBinaryChain(
        buildStatementBitXor(ctx->statementBitXorExpression()),
        ctx->PIPE(),
        [&](std::size_t i){
            return buildBitXor(operands[i]);
        }
    );
}

ast::ExprPtr ASTBuilder::buildStatementBitXor(rx::Parser::StatementBitXorExpressionContext *ctx){
    auto operands = ctx->bitAndExpression();
    return buildBinaryChain(
        buildStatementBitAnd(ctx->statementBitAndExpression()),
        ctx->CARET(),
        [&](std::size_t i){
            return buildBitAnd(operands[i]);
        }
    );
}

ast::ExprPtr ASTBuilder::buildStatementBitAnd(rx::Parser::StatementBitAndExpressionContext *ctx){
    auto operands = ctx->shiftExpression();
    return buildBinaryChain(
        buildStatementShift(ctx->statementShiftExpression()),
        ctx->AMP(),
        [&](std::size_t i){
            return buildShift(operands[i]);
        }
    );
}

ast::ExprPtr ASTBuilder::buildStatementShift(rx::Parser::StatementShiftExpressionContext *ctx){
    return buildShiftChain(ctx);
}

ast::ExprPtr ASTBuilder::buildStatementClosedBitOr(rx::Parser::StatementClosedBitOrExpressionContext *ctx){
    if (ctx->statementClosedBitXorExpression() != nullptr) {
        return buildStatementClosedBitXor(ctx->statementClosedBitXorExpression());
    }

    auto operands = ctx->bitXorExpression();
    return buildBinaryChain(
        buildStatementBitXor(ctx->statementBitXorExpression()),
        ctx->PIPE(),
        [&](std::size_t i){
            if (i < operands.size()) {
                return buildBitXor(operands[i]);
            }
            return buildClosedBitXor(ctx->closedBitXorExpression());
        }
    );
}

ast::ExprPtr ASTBuilder::buildStatementClosedBitXor(rx::Parser::StatementClosedBitXorExpressionContext *ctx){
    if (ctx->statementClosedBitAndExpression() != nullptr) {
        return buildStatementClosedBitAnd(ctx->statementClosedBitAndExpression());
    }

    auto operands = ctx->bitAndExpression();
    return buildBinaryChain(
        buildStatementBitAnd(ctx->statementBitAndExpression()),
        ctx->CARET(),
        [&](std::size_t i){
            if (i < operands.size()) {
                return buildBitAnd(operands[i]);
            }
            return buildClosedBitAnd(ctx->closedBitAndExpression());
        }
    );
}

ast::ExprPtr ASTBuilder::buildStatementClosedBitAnd(rx::Parser::StatementClosedBitAndExpressionContext *ctx){
    if (ctx->statementClosedShiftExpression() != nullptr) {
        return buildStatementClosedShift(ctx->statementClosedShiftExpression());
    }

    auto operands = ctx->shiftExpression();
    return buildBinaryChain(
        buildStatementShift(ctx->statementShiftExpression()),
        ctx->AMP(),
        [&](std::size_t i){
            if (i < operands.size()) {
                return buildShift(operands[i]);
            }
            return buildClosedShift(ctx->closedShiftExpression());
        }
    );
}

ast::ExprPtr ASTBuilder::buildStatementClosedShift(rx::Parser::StatementClosedShiftExpressionContext *ctx){
    return buildShiftChain(ctx);
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
    if (ctx->statementUnaryExpression() != nullptr) {
        return buildStatementUnary(ctx->statementUnaryExpression());
    }
    return std::make_unique<ast::CastExpr>(
        buildStatementCast(ctx->statementCastExpression()),
        buildClosedCastType(ctx->closedCastType())
    );
}

ast::ExprPtr ASTBuilder::buildConditionBitOr(rx::Parser::ConditionBitOrExpressionContext *ctx){
    auto operands = ctx->conditionBitXorExpression();
    return buildBinaryChain(
        buildConditionBitXor(operands[0]),
        ctx->PIPE(),
        [&](std::size_t i){
            return buildConditionBitXor(operands[i + 1]);
        }
    );
}

ast::ExprPtr ASTBuilder::buildConditionBitXor(rx::Parser::ConditionBitXorExpressionContext *ctx){
    auto operands = ctx->conditionBitAndExpression();
    return buildBinaryChain(
        buildConditionBitAnd(operands[0]),
        ctx->CARET(),
        [&](std::size_t i){
            return buildConditionBitAnd(operands[i + 1]);
        }
    );
}

ast::ExprPtr ASTBuilder::buildConditionBitAnd(rx::Parser::ConditionBitAndExpressionContext *ctx){
    auto operands = ctx->conditionShiftExpression();
    return buildBinaryChain(
        buildConditionShift(operands[0]),
        ctx->AMP(),
        [&](std::size_t i){
            return buildConditionShift(operands[i + 1]);
        }
    );
}

ast::ExprPtr ASTBuilder::buildConditionShift(rx::Parser::ConditionShiftExpressionContext *ctx){
    return buildShiftChain(ctx);
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
    return buildCastChain(buildConditionUnary(ctx->conditionUnaryExpression()), ctx->typeRef());
}

ast::ExprPtr ASTBuilder::buildConditionClosedBitOr(rx::Parser::ConditionClosedBitOrExpressionContext *ctx){
    auto operands = ctx->conditionBitXorExpression();
    if (operands.empty()) {
        return buildConditionClosedBitXor(ctx->conditionClosedBitXorExpression());
    }
    return buildBinaryChain(
        buildConditionBitXor(operands[0]),
        ctx->PIPE(),
        [&](std::size_t i){
            if (i + 1 < operands.size()) {
                return buildConditionBitXor(operands[i + 1]);
            }
            return buildConditionClosedBitXor(ctx->conditionClosedBitXorExpression());
        }
    );
}

ast::ExprPtr ASTBuilder::buildConditionClosedBitXor(rx::Parser::ConditionClosedBitXorExpressionContext *ctx){
    auto operands = ctx->conditionBitAndExpression();
    if (operands.empty()) {
        return buildConditionClosedBitAnd(ctx->conditionClosedBitAndExpression());
    }
    return buildBinaryChain(
        buildConditionBitAnd(operands[0]),
        ctx->CARET(),
        [&](std::size_t i){
            if (i + 1 < operands.size()) {
                return buildConditionBitAnd(operands[i + 1]);
            }
            return buildConditionClosedBitAnd(ctx->conditionClosedBitAndExpression());
        }
    );
}

ast::ExprPtr ASTBuilder::buildConditionClosedBitAnd(rx::Parser::ConditionClosedBitAndExpressionContext *ctx){
    auto operands = ctx->conditionShiftExpression();
    if (operands.empty()) {
        return buildConditionClosedShift(ctx->conditionClosedShiftExpression());
    }
    return buildBinaryChain(
        buildConditionShift(operands[0]),
        ctx->AMP(),
        [&](std::size_t i){
            if (i + 1 < operands.size()) {
                return buildConditionShift(operands[i + 1]);
            }
            return buildConditionClosedShift(ctx->conditionClosedShiftExpression());
        }
    );
}

ast::ExprPtr ASTBuilder::buildConditionClosedShift(rx::Parser::ConditionClosedShiftExpressionContext *ctx){
    return buildShiftChain(ctx);
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
    if (ctx->conditionUnaryExpression() != nullptr) {
        return buildConditionUnary(ctx->conditionUnaryExpression());
    }
    return std::make_unique<ast::CastExpr>(
        buildConditionCast(ctx->conditionCastExpression()),
        buildClosedCastType(ctx->closedCastType())
    );
}

ast::ExprPtr ASTBuilder::buildConditionUnary(rx::Parser::ConditionUnaryExpressionContext *ctx){
    if (ctx->unaryOperator() != nullptr) {
        // 递归先构建内层，使前缀运算从右向左嵌套。
        auto operand = buildConditionUnary(ctx->conditionUnaryExpression());
        return buildUnaryOperator(ctx->unaryOperator(), std::move(operand));
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
            return std::make_unique<ast::UnitExpr>();
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

// 条件 break 的首个操作数使用专用规则，后续操作数使用普通条件规则。
ast::ExprPtr ASTBuilder::buildConditionBreakBitOr(rx::Parser::ConditionBreakBitOrExpressionContext *ctx){
    auto operands = ctx->conditionBitXorExpression();
    return buildBinaryChain(
        buildConditionBreakBitXor(ctx->conditionBreakBitXorExpression()),
        ctx->PIPE(),
        [&](std::size_t i){
            return buildConditionBitXor(operands[i]);
        }
    );
}

ast::ExprPtr ASTBuilder::buildConditionBreakBitXor(rx::Parser::ConditionBreakBitXorExpressionContext *ctx){
    auto operands = ctx->conditionBitAndExpression();
    return buildBinaryChain(
        buildConditionBreakBitAnd(ctx->conditionBreakBitAndExpression()),
        ctx->CARET(),
        [&](std::size_t i){
            return buildConditionBitAnd(operands[i]);
        }
    );
}

ast::ExprPtr ASTBuilder::buildConditionBreakBitAnd(rx::Parser::ConditionBreakBitAndExpressionContext *ctx){
    auto operands = ctx->conditionShiftExpression();
    return buildBinaryChain(
        buildConditionBreakShift(ctx->conditionBreakShiftExpression()),
        ctx->AMP(),
        [&](std::size_t i){
            return buildConditionShift(operands[i]);
        }
    );
}

ast::ExprPtr ASTBuilder::buildConditionBreakShift(rx::Parser::ConditionBreakShiftExpressionContext *ctx){
    return buildShiftChain(ctx);
}

ast::ExprPtr ASTBuilder::buildConditionBreakClosedBitOr(rx::Parser::ConditionBreakClosedBitOrExpressionContext *ctx){
    if (ctx->conditionBreakClosedBitXorExpression() != nullptr) {
        return buildConditionBreakClosedBitXor(ctx->conditionBreakClosedBitXorExpression());
    }

    auto operands = ctx->conditionBitXorExpression();
    return buildBinaryChain(
        buildConditionBreakBitXor(ctx->conditionBreakBitXorExpression()),
        ctx->PIPE(),
        [&](std::size_t i){
            if (i < operands.size()) {
                return buildConditionBitXor(operands[i]);
            }
            return buildConditionClosedBitXor(ctx->conditionClosedBitXorExpression());
        }
    );
}

ast::ExprPtr ASTBuilder::buildConditionBreakClosedBitXor(rx::Parser::ConditionBreakClosedBitXorExpressionContext *ctx){
    if (ctx->conditionBreakClosedBitAndExpression() != nullptr) {
        return buildConditionBreakClosedBitAnd(ctx->conditionBreakClosedBitAndExpression());
    }

    auto operands = ctx->conditionBitAndExpression();
    return buildBinaryChain(
        buildConditionBreakBitAnd(ctx->conditionBreakBitAndExpression()),
        ctx->CARET(),
        [&](std::size_t i){
            if (i < operands.size()) {
                return buildConditionBitAnd(operands[i]);
            }
            return buildConditionClosedBitAnd(ctx->conditionClosedBitAndExpression());
        }
    );
}

ast::ExprPtr ASTBuilder::buildConditionBreakClosedBitAnd(rx::Parser::ConditionBreakClosedBitAndExpressionContext *ctx){
    if (ctx->conditionBreakClosedShiftExpression() != nullptr) {
        return buildConditionBreakClosedShift(ctx->conditionBreakClosedShiftExpression());
    }

    auto operands = ctx->conditionShiftExpression();
    return buildBinaryChain(
        buildConditionBreakShift(ctx->conditionBreakShiftExpression()),
        ctx->AMP(),
        [&](std::size_t i){
            if (i < operands.size()) {
                return buildConditionShift(operands[i]);
            }
            return buildConditionClosedShift(ctx->conditionClosedShiftExpression());
        }
    );
}

ast::ExprPtr ASTBuilder::buildConditionBreakClosedShift(rx::Parser::ConditionBreakClosedShiftExpressionContext *ctx){
    return buildShiftChain(ctx);
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
    return buildCastChain(buildConditionBreakUnary(ctx->conditionBreakUnaryExpression()), ctx->typeRef());
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
    if (ctx->conditionBreakUnaryExpression() != nullptr) {
        return buildConditionBreakUnary(ctx->conditionBreakUnaryExpression());
    }
    return std::make_unique<ast::CastExpr>(
        buildConditionBreakCast(ctx->conditionBreakCastExpression()),
        buildClosedCastType(ctx->closedCastType())
    );
}

ast::ExprPtr ASTBuilder::buildConditionBreakUnary(rx::Parser::ConditionBreakUnaryExpressionContext *ctx){
    if (ctx->unaryOperator() != nullptr) {
        // 前缀运算符后的操作数使用语法规定的普通表达式规则。
        auto operand = buildConditionUnary(ctx->conditionUnaryExpression());
        return buildUnaryOperator(ctx->unaryOperator(), std::move(operand));
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
        return buildTypePath(ctx->typePath());
    }

    if (auto *reference = ctx->referenceType()) {
        return buildReferenceType(reference, buildTypeRef(reference->typeRef()));
    }

    if (ctx->arrayType() != nullptr) {
        return buildArrayType(ctx->arrayType());
    }

    if (ctx->LPAREN() != nullptr) {
        // (T) 只分组，不增加类型节点；没有内层类型时才是单元类型 ()。
        if (ctx->typeRef() != nullptr) {
            return buildTypeRef(ctx->typeRef());
        }
        return std::make_unique<ast::UnitTypeRef>();
    }

    throw std::runtime_error("this type form is not supported yet");
}

std::unique_ptr<ast::TypeRef> ASTBuilder::buildClosedCastType(rx::Parser::ClosedCastTypeContext *ctx) {
    // closedCastType 约束类型的结尾，避免把后续 < 或 << 读成泛型参数。
    if (ctx->LPAREN() != nullptr) {
        return ctx->typeRef() != nullptr ? buildTypeRef(ctx->typeRef())
                                        : std::make_unique<ast::UnitTypeRef>();
    }
    if (ctx->arrayType() != nullptr) {
        return buildArrayType(ctx->arrayType());
    }
    if (ctx->closedCastType() != nullptr) {
        return buildReferenceType(ctx, buildClosedCastType(ctx->closedCastType()));
    }
    // closedCastType 的最后一段由独立的 pathIdentSegment/genericArgs 表示。
    std::vector<std::unique_ptr<ast::PathSegment>> segments;
    for (auto *segmentCtx : ctx->typePathSegment()) {
        segments.push_back(buildTypePathSegment(segmentCtx));
    }
    segments.push_back(std::make_unique<ast::PathSegment>(
        ctx->pathIdentSegment()->getText(), buildGenericArgs(ctx->genericArgs())
    ));
    return std::make_unique<ast::TypePathRef>(std::move(segments));
}

std::unique_ptr<ast::ArrayTypeRef> ASTBuilder::buildArrayType(rx::Parser::ArrayTypeContext *ctx) {
    // [[T; N]; M] 的内层数组仍交给类型入口递归构建。
    auto elementType = buildTypeRef(ctx->typeRef());
    auto count = buildConstValue(ctx->constValue());

    return std::make_unique<ast::ArrayTypeRef>(std::move(elementType), std::move(count));
}

std::unique_ptr<ast::SelfFunctionParam> ASTBuilder::buildSelfParam(rx::Parser::SelfParamContext *ctx) {
    bool isReference = ctx->AMP() != nullptr;
    bool isMutable = ctx->MUT() != nullptr;

    return std::make_unique<ast::SelfFunctionParam>(isReference, isMutable);
}

}
