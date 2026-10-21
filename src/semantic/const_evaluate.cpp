#include "const_evaluate.hpp"
#include "semantic_checker.hpp"

#include <algorithm>
#include <initializer_list>
#include <utility>

std::optional<ConstValue> ConstEvaluate::evaluateConstant(SymbolId symbol){
    auto &constant = checker_.model_.declarations.constants.at(symbol);
    const auto &expression = constant.declaration->value();
    const auto state = checker_.constantStates_.find(symbol);
    if(state != checker_.constantStates_.end()){
        if(state->second == SemanticChecker::ConstantState::Complete) return checker_.model_.constantValues.at(expression.id());
        if(state->second == SemanticChecker::ConstantState::Evaluating){
            checker_.diagnose(*constant.declaration, "cyclic constant dependency involving '" + constant.declaration->name() + "'");
        }
        return std::nullopt;
    }
    checker_.constantStates_.emplace(symbol, SemanticChecker::ConstantState::Evaluating);
    struct ContextGuard{
        SemanticChecker &checker;
        SymbolId symbol;
        std::optional<TypeId> previousSelfType;
        std::optional<Scope> previousScope;
        ~ContextGuard(){
            checker.currentSelfType_ = previousSelfType;
            if(previousScope.has_value()) checker.scope_ = std::move(*previousScope);
            if(checker.constantStates_.at(symbol) == SemanticChecker::ConstantState::Evaluating){
                checker.constantStates_.at(symbol) = SemanticChecker::ConstantState::Failed;
            }
        }
    } context{checker_, symbol, checker_.currentSelfType_, std::nullopt};
    // 常量初始化式按声明环境解析，不能捕获求值调用处的局部变量。
    if(checker_.scope_.depth() != 0){
        context.previousScope = checker_.scope_;
        while(checker_.scope_.depth() != 0) checker_.scope_.popScope();
    }
    checker_.currentSelfType_ = constant.selfType;
    // 长度表达式可能提前引用尚未收集类型的常量，因此按需解析声明类型。
    if(constant.type.value == 0) checker_.declarationCollector_.collectConstantType(*constant.declaration, constant.selfType);
    if(constant.type == checker_.model_.types.builtin(TypeKind::Error)) return std::nullopt;
    const auto value = evaluateConstantExpr(expression, constant.type);
    if(value.has_value()) checker_.constantStates_.at(symbol) = SemanticChecker::ConstantState::Complete;
    return value;
}

std::optional<ConstValue> ConstEvaluate::evaluateConstantExpr(const rx::ast::Expr &expression, TypeId expectedType){
    const TypeId errorType = checker_.model_.types.builtin(TypeKind::Error);
    if(expectedType == errorType) return std::nullopt;
    auto fail = [&](const std::string &message) -> std::optional<ConstValue>{
        checker_.diagnose(expression, message);
        checker_.model_.expressionTypes.insert_or_assign(expression.id(), errorType);
        return std::nullopt;
    };
    const rx::ast::Expr *operand = &expression;
    bool negative = false;
    if(const auto *unary = dynamic_cast<const rx::ast::UnaryExpr *>(&expression)){
        if(unary->op() != "-") return fail("unsupported operator in constant expression");
        operand = &unary->operand();
        negative = true;
        if(dynamic_cast<const rx::ast::IntegerLiteralExpr *>(operand) == nullptr &&
           dynamic_cast<const rx::ast::PathExpr *>(operand) == nullptr){
            return fail("constant negation requires an integer literal or constant path");
        }
    }

    ConstValue result;
    if(const auto *literal = dynamic_cast<const rx::ast::IntegerLiteralExpr *>(operand)){
        std::string text = literal->text();
        text.erase(std::remove(text.begin(), text.end(), '_'), text.end());
        TypeKind kind = TypeKind::I32;
        if(!negative && checker_.model_.types.get(expectedType).isInteger()) kind = checker_.model_.types.get(expectedType).kind();
        for(const auto &[suffix, suffixKind] : std::initializer_list<std::pair<std::string, TypeKind>>{
            {"i32", TypeKind::I32}, {"u32", TypeKind::U32}, {"isize", TypeKind::Isize}, {"usize", TypeKind::Usize}}){
            if(text.size() >= suffix.size() && text.compare(text.size() - suffix.size(), suffix.size(), suffix) == 0){
                kind = suffixKind;
                text.resize(text.size() - suffix.size());
                break;
            }
        }
        result.type = checker_.model_.types.builtin(kind);
        const bool isSigned = checker_.model_.types.get(result.type).isSignedInteger();
        if(negative && !isSigned) return fail("constant negation requires a signed integer");
        const std::uint64_t limit = isSigned ? (negative ? 2147483648ull : 2147483647ull) : 4294967295ull;
        unsigned base = 10;
        std::size_t start = 0;
        if(text.size() >= 2 && text[0] == '0'){
            if(text[1] == 'x'){ base = 16; start = 2; }
            else if(text[1] == 'o'){ base = 8; start = 2; }
            else if(text[1] == 'b'){ base = 2; start = 2; }
        }
        if(start == text.size()) return fail("integer literal has no digits");
        std::uint64_t magnitude = 0;
        for(std::size_t index = start; index < text.size(); ++index){
            const char character = text[index];
            unsigned digit = 16;
            if(character >= '0' && character <= '9') digit = character - '0';
            else if(character >= 'a' && character <= 'f') digit = character - 'a' + 10;
            else if(character >= 'A' && character <= 'F') digit = character - 'A' + 10;
            if(digit >= base) return fail("invalid integer literal in constant expression");
            if(magnitude > (limit - digit) / base) return fail("integer literal is out of range");
            magnitude = magnitude * base + digit;
        }
        result.bits = static_cast<std::uint32_t>(magnitude);
    }else if(const auto *literal = dynamic_cast<const rx::ast::BooleanLiteralExpr *>(operand)){
        result = ConstValue{checker_.model_.types.builtin(TypeKind::Bool), literal->value() ? 1u : 0u};
    }else if(const auto *path = dynamic_cast<const rx::ast::PathExpr *>(operand)){
        const auto symbol = checker_.resolution_.resolveValuePath(*path);
        if(!symbol.has_value()) return std::nullopt;
        if(checker_.model_.declarations.constants.find(*symbol) == checker_.model_.declarations.constants.end()){
            return fail("constant path must name a constant item");
        }
        const auto value = evaluateConstant(*symbol);
        if(!value.has_value()) return std::nullopt;
        result = *value;
    }else{
        return fail("unsupported expression in constant context");
    }

    checker_.model_.expressionTypes.insert_or_assign(operand->id(), result.type);
    if(negative){
        if(!checker_.model_.types.get(result.type).isSignedInteger()) return fail("constant negation requires a signed integer");
        // 用无符号运算实现 32 位取负，避免宿主 C++ 的有符号溢出。
        result.bits = std::uint32_t{0} - result.bits;
    }
    checker_.model_.expressionTypes.insert_or_assign(expression.id(), result.type);
    if(result.type != expectedType) return fail("constant value does not match expected type");
    checker_.model_.constantValues.insert_or_assign(expression.id(), result);
    return result;
}

std::optional<std::uint32_t> ConstEvaluate::evaluateArrayLength(const rx::ast::Expr &expression){
    const auto value = evaluateConstantExpr(expression, checker_.model_.types.builtin(TypeKind::Usize));
    if(!value.has_value()) return std::nullopt;
    return value->bits;
}
