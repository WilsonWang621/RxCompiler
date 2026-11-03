#include "ir_generator.hpp"

#include <stdexcept>

// 内置调用需要的最小表达式支持；完整运算和 place 生成仍待补充。
IRExprResult IRGenerator::generateExpr(const rx::ast::Expr &expression){
    const auto &model = *context_.model;
    if(const auto *call = dynamic_cast<const rx::ast::CallExpr *>(&expression)) return generateCall(*call);
    if(const auto *block = dynamic_cast<const rx::ast::BlockExpr *>(&expression)) return generateBlock(*block);
    if(const auto *ret = dynamic_cast<const rx::ast::ReturnExpr *>(&expression)) return generateReturn(*ret);
    if(dynamic_cast<const rx::ast::UnitExpr *>(&expression)) return {};
    const auto constant = model.constantValues.find(expression.id());
    if(constant != model.constantValues.end()){
        return {IRValue{context_.typeLowering.lowerType(constant->second.type, model), "", constant->second.bits}, false};
    }
    if(const auto *path = dynamic_cast<const rx::ast::PathExpr *>(&expression)) return generatePath(*path);
    if(const auto *binary = dynamic_cast<const rx::ast::BinaryExpr *>(&expression)) return generateBinary(*binary);
    if(const auto *assignment = dynamic_cast<const rx::ast::AssignExpr *>(&expression)) return generateAssignment(*assignment);
    if(const auto *compoundAssignment = dynamic_cast<const rx::ast::CompoundAssignExpr *>(&expression)) return generateCompoundAssignment(*compoundAssignment);
    if(const auto *unary = dynamic_cast<const rx::ast::UnaryExpr *>(&expression)) return generateUnary(*unary);
    throw std::runtime_error("IR expression is not implemented");
}

IRExprResult IRGenerator::generatePath(const rx::ast::PathExpr &expression){
    const auto &model = *context_.model;
    const SymbolId symbol = model.resolvedSymbols.at(expression.id());
    //if 已经是登记过的常量，直接取值
    const auto constant = context_.constants.find(symbol);
    if(constant != context_.constants.end()) return {constant->second, false};
    //otherwise 在context_.local中找它的地址，然后去load
    const IRType type = context_.typeLowering.lowerType(model.symbolTypes.at(symbol), model);
    if(type == IRType::Void) return {};
    return {context_.builder.createLoad(type, context_.locals.at(symbol)), false};
}

IRExprResult IRGenerator::generateCall(const rx::ast::CallExpr &expression){
    const auto &model = *context_.model;
    const SymbolId symbol = model.resolvedSymbols.at(expression.id());
    const auto &callee = *context_.functions.at(symbol);
    std::vector<IRValue> arguments;
    for(const auto &argument : expression.arguments()){
        const auto result = generateExpr(*argument);
        if(result.terminated) return result;
        if(!result.value) throw std::runtime_error("IR unit arguments are not supported");
        arguments.push_back(*result.value);
    }
    return {context_.builder.createCall(callee, std::move(arguments)), false};
}

IRExprResult IRGenerator::generateBinary(const rx::ast::BinaryExpr &expression){
    const auto &model = *context_.model;
    const std::string op = expression.op();

    if(op == "||" || op == "&&"){
        throw std::runtime_error("IR logical operators are not implemented");
    }

    const auto left = generateExpr(expression.left());
    if(left.terminated) return left;

    const auto right = generateExpr(expression.right());
    if(right.terminated) return right;

    if(!left.value || !right.value){
        throw std::runtime_error("IR binary operands need value");
    }

    const TypeId leftType = model.expressionTypes.at(expression.left().id());
    const TypeId rightType = model.expressionTypes.at(expression.right().id());
    const auto leftInfo = model.types.get(leftType);
    const auto rightInfo = model.types.get(rightType);

    if(op == "+"){
        return {context_.builder.createBinary(IROpcode::Add, *left.value, *right.value), false};
    }else if(op == "-"){
        return {context_.builder.createBinary(IROpcode::Sub, *left.value, *right.value), false};
    }else if(op == "*"){
        return {context_.builder.createBinary(IROpcode::Mul, *left.value, *right.value), false};
    }else if(op == "/"){
        if(leftInfo.isSignedInteger()){
            return {context_.builder.createBinary(IROpcode::SDiv, *left.value, *right.value), false};
        }
        else{
            return {context_.builder.createBinary(IROpcode::UDiv, *left.value, *right.value), false};
        }
    }else if(op == "%"){
        if(leftInfo.isSignedInteger()){
            return {context_.builder.createBinary(IROpcode::SRem, *left.value, *right.value), false};
        }
        else{
            return {context_.builder.createBinary(IROpcode::URem, *left.value, *right.value), false};
        }
    }

    const bool isSigned = leftInfo.isSignedInteger();
    IRComparePredicate predicate;
    if(op == "=="){
        predicate = IRComparePredicate::Eq;
    }else if(op == "!="){
        predicate = IRComparePredicate::Ne;
    }else if(op == "<"){
        predicate = isSigned ? IRComparePredicate::Slt : IRComparePredicate::Ult;
    }else if(op == "<="){
        predicate = isSigned ? IRComparePredicate::Sle : IRComparePredicate::Ule;
    }else if(op == ">"){
        predicate = isSigned ? IRComparePredicate::Sgt : IRComparePredicate::Ugt;
    }else if(op == ">="){
        predicate = isSigned ? IRComparePredicate::Sge : IRComparePredicate::Uge;
    }else{
        throw std::runtime_error("IR binary operator is not implemented: " + op);
    }

    return {context_.builder.createCompare(predicate, *left.value, *right.value), false};
}

std::optional<IRPlace> IRGenerator::generatePlace(const rx::ast::Expr &expression){
    const auto model = context_.model;
    if(const auto *path = dynamic_cast<const rx::ast::PathExpr *>(&expression)){
        const SymbolId symbol = model->resolvedSymbols.at(path->id());
        return IRPlace{
            context_.locals.at(symbol),
            model->symbolTypes.at(symbol)
        };
    }

    if(const auto *unary = dynamic_cast<const rx::ast::UnaryExpr *>(&expression)){
        if(unary->op() == "*"){
            const auto pointer = generateExpr(unary->operand());
            if(pointer.terminated) return std::nullopt;

            if(!pointer.value || pointer.value->type != IRType::Ptr){
                throw std::runtime_error("IR dereference requires a pointer");
            }
            return IRPlace{
                *pointer.value,
                model->expressionTypes.at(expression.id())
            };
        }
    }
    throw std::runtime_error("IR place expression is not implemented");
}

IRExprResult IRGenerator::generateAssignment(const rx::ast::AssignExpr &expression){
    const auto value = generateExpr(expression.value());
    if(value.terminated) return value;  //如果生成右侧时已经终结当前基本块，就直接返回，不能继续生成赋值指令 eg. return

    const auto place = generatePlace(expression.target());
    if(!place) return {{}, true};

    if(!value.value){
        throw std::runtime_error("IR assignment requires a value");
    }
    const IRType type = context_.typeLowering.lowerType(place->type, *context_.model);
    if(value.value->type != type) throw std::runtime_error("IR assignment value type does not match target");

    context_.builder.createStore(*value.value, place->address);
    return {};
}

IRExprResult IRGenerator::generateCompoundAssignment(const rx::ast::CompoundAssignExpr &expression){
    const auto value = generateExpr(expression.value());
    if(value.terminated) return value;

    const auto place = generatePlace(expression.target());
    if(!place) return {{}, true};

    if(!value.value){
        throw std::runtime_error("IR compound assignment requires a value");
    }

    const auto &model = *context_.model;
    const auto &target = model.types.get(place->type);
    const IRType type = context_.typeLowering.lowerType(place->type, model);
    IRValue right = *value.value;
    const auto &rightInfo = model.types.get(model.expressionTypes.at(expression.value().id()));
    // 复合赋值的右侧允许一层共享引用，先读取其指向的标量。
    if(rightInfo.kind() == TypeKind::Reference && !rightInfo.isMutable()){
        right = context_.builder.createLoad(context_.typeLowering.lowerType(rightInfo.elementType(), model), right);
    }
    if(right.type != type) throw std::runtime_error("IR compound assignment value type does not match target");

    const std::string &op = expression.op();
    IROpcode opcode;
    if(op == "+=") opcode = IROpcode::Add;
    else if(op == "-=") opcode = IROpcode::Sub;
    else if(op == "*=") opcode = IROpcode::Mul;
    else if(op == "/=") opcode = target.isSignedInteger() ? IROpcode::SDiv : IROpcode::UDiv;
    else if(op == "%=") opcode = target.isSignedInteger() ? IROpcode::SRem : IROpcode::URem;
    else if(op == "&=") opcode = IROpcode::And;
    else if(op == "|=") opcode = IROpcode::Or;
    else if(op == "^=") opcode = IROpcode::Xor;
    else if(op == "<<=") opcode = IROpcode::Shl;
    else if(op == ">>=") opcode = target.isSignedInteger() ? IROpcode::AShr : IROpcode::LShr;
    else throw std::runtime_error("IR compound assignment operator is not implemented: " + op);

    // Rx 的 32 位移位只使用移位量的低 5 位。
    if(op == "<<=" || op == ">>="){
        right = context_.builder.createBinary(IROpcode::And, right, IRValue{IRType::I32, "", 31});
    }
    IRValue old = context_.builder.createLoad(type, place->address);
    const auto result = context_.builder.createBinary(opcode, old, right);
    context_.builder.createStore(result, place->address);
    return {};
}

IRExprResult IRGenerator::generateUnary(const rx::ast::UnaryExpr &expression){
    const auto &model = *context_.model;
    const std::string op = expression.op();
    const auto &operand = expression.operand();

    if(op == "&" || op == "&mut"){
        const auto &info = model.expressionInfo.at(operand.id());
        if(info.isPlace){
            const auto place = generatePlace(operand);
            if(!place) return {{}, true};
            return {place->address, false};
        }

        const auto value = generateExpr(operand);
        if(value.terminated) return value;
        if(!value.value){
            throw std::runtime_error("IR borrowing this value is not supported");
        }

        const auto address = context_.builder.createAlloca(value.value->type);
        context_.builder.createStore(*value.value, address);
        return {address, false};
    }

    if(op == "*"){
        const auto place = generatePlace(expression);
        if(!place) return {{}, true};

        const IRType type = context_.typeLowering.lowerType(place->type, model);
        if(type == IRType::Void) return {};

        return{
            context_.builder.createLoad(type, place->address),
            false
        };
    }

    const auto value = generateExpr(operand);
    if(value.terminated) return value;
    if(!value.value){
        throw std::runtime_error("IR unary operand requires a value");
    }

    IRValue scalar = *value.value;
    const auto &operandType = model.types.get(model.expressionTypes.at(operand.id()));

    if(operandType.kind() == TypeKind::Reference && !operandType.isMutable()){
        const IRType type = context_.typeLowering.lowerType(operandType.elementType(), model);
        scalar = context_.builder.createLoad(type, scalar);
    }

    if(op == "-"){
        return {
            context_.builder.createBinary(
                IROpcode::Sub,
                IRValue{IRType::I32, "", 0},
                scalar
            ),
            false
        };
    }

    if(op == "!"){
        const std::uint32_t mask =
            scalar.type == IRType::I1 ? 1u : 0xffffffffu;

        return {
            context_.builder.createBinary(
                IROpcode::Xor,
                scalar,
                IRValue{scalar.type, "", mask}
            ),
            false
        };
    }

    throw std::runtime_error("IR unary operator is not implemented: " + op);
}
