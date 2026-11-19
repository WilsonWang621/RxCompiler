#include "ir_generator.hpp"

#include <stdexcept>

// 标量、数组、结构体、调用和控制流表达式分发。
IRExprResult IRGenerator::generateExpr(const rx::ast::Expr &expression){
    const auto &model = *context_.model;
    if(const auto *call = dynamic_cast<const rx::ast::CallExpr *>(&expression)) return generateCall(*call);
    if(const auto *block = dynamic_cast<const rx::ast::BlockExpr *>(&expression)) return generateBlock(*block);
    if(const auto *ret = dynamic_cast<const rx::ast::ReturnExpr *>(&expression)) return generateReturn(*ret);
    if(const auto *ifExpr = dynamic_cast<const rx::ast::IfExpr *>(&expression)) return generateIf(*ifExpr);
    if(const auto *loopExpr = dynamic_cast<const rx::ast::LoopExpr *>(&expression)) return generateLoop(*loopExpr);
    if(const auto *whileExpr = dynamic_cast<const rx::ast::WhileExpr *>(&expression)) return generateWhile(*whileExpr);
    if(const auto *breakExpr = dynamic_cast<const rx::ast::BreakExpr *>(&expression)) return generateBreak(*breakExpr);
    if(const auto *continueExpr = dynamic_cast<const rx::ast::ContinueExpr *>(&expression)) return generateContinue(*continueExpr);
    if(dynamic_cast<const rx::ast::UnitExpr *>(&expression)) return {};
    const auto constant = model.constantValues.find(expression.id());
    if(constant != model.constantValues.end()){
        return {IRValue{context_.typeLowering.lowerStorageType(constant->second.type, model), "", constant->second.bits}, false};
    }
    if(const auto *path = dynamic_cast<const rx::ast::PathExpr *>(&expression)) return generatePath(*path);
    if(const auto *binary = dynamic_cast<const rx::ast::BinaryExpr *>(&expression)) return generateBinary(*binary);
    if(const auto *assignment = dynamic_cast<const rx::ast::AssignExpr *>(&expression)) return generateAssignment(*assignment);
    if(const auto *compoundAssignment = dynamic_cast<const rx::ast::CompoundAssignExpr *>(&expression)) return generateCompoundAssignment(*compoundAssignment);
    if(const auto *unary = dynamic_cast<const rx::ast::UnaryExpr *>(&expression)) return generateUnary(*unary);
    if(const auto *cast = dynamic_cast<const rx::ast::CastExpr *>(&expression)) return generateCast(*cast);
    if(const auto *structure = dynamic_cast<const rx::ast::StructExpr *>(&expression)) return generateStruct(*structure);
    if(const auto *field = dynamic_cast<const rx::ast::FieldExpr *>(&expression)){
        const auto place = generateFieldPlace(*field);
        if(!place) return {{}, true};
        const IRType type = context_.typeLowering.lowerStorageType(place->type, model);
        return {context_.builder.createLoad(type, place->address), false};
    }
    if(const auto *array = dynamic_cast<const rx::ast::ArrayExpr *>(&expression)) return generateArray(*array);
    if(const auto *array = dynamic_cast<const rx::ast::ArrayRepeatExpr *>(&expression)) return generateRepeatedArray(*array);
    if(const auto *index = dynamic_cast<const rx::ast::IndexExpr *>(&expression)){
        const auto place = generateIndexPlace(*index);
        if(!place) return {{}, true};
        const IRType type = context_.typeLowering.lowerStorageType(place->type, *context_.model);
        return {context_.builder.createLoad(type, place->address), false};
    }
    if(const auto method = dynamic_cast<const rx::ast::MethodCallExpr *>(&expression)) return generateMethodCall(*method);
    throw std::runtime_error("IR expression is not implemented");
}

IRValue IRGenerator::materializeValue(const IRExprResult &result, TypeId type) const{
    if(result.terminated) throw std::logic_error("terminated IR expression cannot produce a value");
    const IRType lowered = context_.typeLowering.lowerStorageType(type, *context_.model);
    if(lowered == IRType::Unit && !result.value) return IRValue{IRType::Unit, "", 0};
    if(!result.value || result.value->type != lowered){
        throw std::logic_error("IR expression result does not match its storage type");
    }
    return *result.value;
}

IRExprResult IRGenerator::generateCoercedExpr(const rx::ast::Expr &expression){
    const auto result = generateExpr(expression);
    const auto plan = context_.model->coercions.find(expression.id());
    return plan == context_.model->coercions.end() ? result : applyCoercion(result, plan->second);
}

IRExprResult IRGenerator::applyCoercion(const IRExprResult &result, const CoercionPlan &plan){
    if(result.terminated || plan.kind == CoercionKind::Identity) return result;
    if(plan.kind == CoercionKind::Never) throw std::logic_error("IR never coercion requires terminated control flow");
    const auto value = materializeValue(result, plan.sourceType);
    const auto place = applyDereferences({value, context_.model->types.get(plan.sourceType).elementType()}, plan.dereferences);
    return {place.address, false};
}

IRPlace IRGenerator::applyDereferences(IRPlace place, const std::vector<DereferenceStep> &steps){
    for(const auto &step : steps){
        if(place.type != step.sourceType) throw std::logic_error("IR dereference plan type mismatch");
        place.address = context_.builder.createLoad(IRType::Ptr, place.address);
        place.type = step.targetType;
    }
    return place;
}

std::optional<IRPlace> IRGenerator::generatePlaceOrTemporary(const rx::ast::Expr &expression){
    if(context_.model->expressionInfo.at(expression.id()).isPlace) return generatePlace(expression);
    const auto result = generateExpr(expression);
    if(result.terminated) return std::nullopt;
    const TypeId type = context_.model->expressionTypes.at(expression.id());
    const auto value = materializeValue(result, type);
    const auto address = context_.builder.createEntryAlloca(value.type);
    context_.builder.createStore(value, address);
    return IRPlace{address, type};
}

IRExprResult IRGenerator::generateReceiver(const rx::ast::Expr &expression, const ReceiverAdjustment &adjustment){
    const auto source = generatePlaceOrTemporary(expression);
    if(!source) return {{}, true};
    const auto place = applyDereferences(*source, adjustment.dereferences);
    if(adjustment.borrow != BorrowKind::None) return {place.address, false};
    return {context_.builder.createLoad(context_.typeLowering.lowerStorageType(adjustment.targetType, *context_.model), place.address), false};
}

IRExprResult IRGenerator::generateMethodCall(const rx::ast::MethodCallExpr &expression){
    const auto &call = context_.model->resolvedCalls.at(expression.id());
    const auto receiver = generateReceiver(expression.receiver(), call.receiver.value());
    if(receiver.terminated) return receiver;
    std::vector<IRValue> arguments{materializeValue(receiver, call.parameterTypes.front())};
    for(const auto &argument : expression.arguments()){
        const auto result = generateCoercedExpr(*argument);
        if(result.terminated) return result;
        arguments.push_back(materializeValue(result, call.parameterTypes.at(arguments.size())));
    }
    return emitResolvedCall(call, std::move(arguments));
}

IRExprResult IRGenerator::emitResolvedCall(const ResolvedCall &call, std::vector<IRValue> arguments){
    if(call.target.builtin != BuiltinOperation::None) return emitBuiltinCall(call, std::move(arguments));
    return {context_.builder.createCall(*context_.functions.at(call.target.symbol), std::move(arguments)), false};
}

IRExprResult IRGenerator::emitBuiltinCall(const ResolvedCall &call, std::vector<IRValue> arguments){
    if(!call.target.ownerType.value) return {context_.builder.createCall(*context_.functions.at(call.target.symbol), std::move(arguments)), false};
    if(call.target.builtin == BuiltinOperation::ArrayLen) return {IRValue{IRType::I32, "", context_.model->types.get(call.target.ownerType).length()}, false};
    if(call.target.builtin == BuiltinOperation::Clone && call.returnType == context_.model->types.builtin(TypeKind::Unit)) return {};
    if(call.target.builtin == BuiltinOperation::Clone){
        return {context_.builder.createCall(valueHelper(call.returnType, true), {arguments.at(0)}), false};
    }
    return emitContainerCall(call, arguments);
}

IRExprResult IRGenerator::generateCast(const rx::ast::CastExpr &expression){
    const auto result = generateExpr(expression.value());
    if(result.terminated) return result;
    if(!result.value){
        throw std::runtime_error("IR cast operand requires a value");
    }

    const auto &model = *context_.model;
    const TypeId sourceType = model.expressionTypes.at(expression.value().id());
    const TypeId targetType = model.resolvedTypes.at(expression.type().id());
    const auto &source = model.types.get(sourceType);
    const auto &target = model.types.get(targetType);

    if(!target.isInteger()){
        throw std::runtime_error("IR cast target must be an integer");
    }

    if(source.isInteger()){
        if(result.value->type != IRType::I32){
            throw std::logic_error("IR integer cast operand must have i32 representation");
        }
        // 四种整数均使用 I32，转换保留原来的 32 位模式。
        return result;
    }

    if(source.kind() == TypeKind::Bool){
        return {context_.builder.createZExt(*result.value), false};
    }

    throw std::runtime_error("IR cast source must be an integer or bool");
}

IRExprResult IRGenerator::generatePath(const rx::ast::PathExpr &expression){
    const auto &model = *context_.model;
    const SymbolId symbol = model.resolvedSymbols.at(expression.id());
    //if 已经是登记过的常量，直接取值
    const auto constant = context_.constants.find(symbol);
    if(constant != context_.constants.end()) return {constant->second, false};
    //otherwise 在context_.local中找它的地址，然后去load
    const IRType type = context_.typeLowering.lowerStorageType(model.symbolTypes.at(symbol), model);
    if(type == IRType::Unit) return {};
    return {context_.builder.createLoad(type, context_.locals.at(symbol)), false};
}

IRExprResult IRGenerator::generateStruct(const rx::ast::StructExpr &expression){
    const auto &model = *context_.model;
    const SymbolId symbol = model.resolvedSymbols.at(expression.id());
    const auto &layout = context_.structs.at(symbol);
    const auto &declaration = model.declarations.structs.at(symbol);
    if(!layout.structure) throw std::logic_error("IR struct layout is missing");

    auto &builder = context_.builder;
    const IRType type = IRType::structure(*layout.structure);
    const IRValue address = builder.createEntryAlloca(type);
    // 求值遵循源码顺序，字段地址使用声明布局中的下标。
    for(const auto &field : expression.fields()){
        const auto result = generateCoercedExpr(field->value());
        if(result.terminated) return result;

        const auto value = materializeValue(result, declaration.fieldTypes.at(field->name()));
        const auto fieldAddress = builder.createFieldAddress(*layout.structure, address, layout.fieldIndices.at(field->name()));
        builder.createStore(value, fieldAddress);
    }
    return {builder.createLoad(type, address), false};
}

IRExprResult IRGenerator::generateCall(const rx::ast::CallExpr &expression){
    const auto &model = *context_.model;
    const auto &call = model.resolvedCalls.at(expression.id());
    std::vector<IRValue> arguments;
    for(const auto &argument : expression.arguments()){
        const auto result = generateCoercedExpr(*argument);
        if(result.terminated) return result;
        arguments.push_back(materializeValue(result, call.parameterTypes.at(arguments.size())));
    }
    return emitResolvedCall(call, std::move(arguments));
}

IRExprResult IRGenerator::generateBinary(const rx::ast::BinaryExpr &expression){
    const auto &model = *context_.model;
    const std::string op = expression.op();

    if(op == "||" || op == "&&"){
        return generateLogical(expression);
    }
    if(op == "==" || op == "!=") return generateEquality(expression);

    auto left = generateExpr(expression.left());
    if(left.terminated) return left;

    auto right = generateExpr(expression.right());
    if(right.terminated) return right;

    TypeId leftType = model.expressionTypes.at(expression.left().id());
    TypeId rightType = model.expressionTypes.at(expression.right().id());
    const bool ordering = op == "<" || op == "<=" || op == ">" || op == ">=";

    // 两侧先求值得到值或引用；引用目标在执行运算时才读取
    const auto loadReference = [&](IRExprResult &operand, TypeId &type){
        if(!operand.value || operand.value->type != IRType::Ptr){
            throw std::logic_error("IR reference operand requires a pointer");
        }
        type = model.types.get(type).elementType();
        const IRType lowered = context_.typeLowering.lowerStorageType(type, model);
        // unit 比较只需完成操作数求值，无需读取零大小数据。
        if(lowered == IRType::Unit) operand.value.reset();
        else operand.value = context_.builder.createLoad(lowered, *operand.value);
    };

    if(ordering){
        if(ordering){
            const auto &leftReference = model.types.get(leftType);
            const auto &rightReference = model.types.get(rightType);
            // 排序仅允许最外层右侧 &mut T 调整为左侧的 &T。
            if(leftReference.kind() == TypeKind::Reference && !leftReference.isMutable() && rightReference.kind() == TypeKind::Reference
                && rightReference.isMutable() && leftReference.elementType() == rightReference.elementType()){
                rightType = leftType;
            }
        }
        if(leftType != rightType){
            throw std::logic_error("IR comparison operand types do not match");
        }
        // 引用比较递归比较目标值，而非指针地址；相等比较也允许 &mut
        while(model.types.get(leftType).kind() == TypeKind::Reference){
            loadReference(left, leftType);
            loadReference(right, rightType);
        }
        const auto &scalar = model.types.get(leftType);
        if(!scalar.isInteger() && scalar.kind() != TypeKind::Bool){
            throw std::runtime_error("IR aggregate comparison is not implemented");
        }
    }else{
        // 算术、位运算和移位只读取一层共享引用，不自动穿过 &mut 或 Box。
        if(model.types.get(leftType).kind() == TypeKind::Reference && !model.types.get(leftType).isMutable()) loadReference(left, leftType);
        if(model.types.get(rightType).kind() == TypeKind::Reference && !model.types.get(rightType).isMutable()) loadReference(right, rightType);
        const auto &leftScalar = model.types.get(leftType);
        const auto &rightScalar = model.types.get(rightType);
        if((!leftScalar.isInteger() && leftScalar.kind() != TypeKind::Bool) || (!rightScalar.isInteger() && rightScalar.kind() != TypeKind::Bool)){
            throw std::runtime_error("IR binary operands require supported scalars");
        }
    }

    if(!left.value || !right.value){
        throw std::runtime_error("IR binary operands need value");
    }
    const auto leftInfo = model.types.get(leftType);

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
    }else if(op == "&"){
        return {context_.builder.createBinary(IROpcode::And, *left.value, *right.value), false};
    }else if(op == "|"){
        return {context_.builder.createBinary(IROpcode::Or, *left.value, *right.value), false};
    }else if(op == "^"){
        return {context_.builder.createBinary(IROpcode::Xor, *left.value, *right.value), false};
    }else if(op == "<<" || op == ">>"){
        const IRValue amount = context_.builder.createBinary(IROpcode::And, *right.value, IRValue{IRType::I32, "", 31});
        const IROpcode opcode = op == "<<" ? IROpcode::Shl : (leftInfo.isSignedInteger() ? IROpcode::AShr : IROpcode::LShr);
        return {context_.builder.createBinary(opcode, *left.value, amount), false};
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

    if(const auto *field = dynamic_cast<const rx::ast::FieldExpr *>(&expression)){
        return generateFieldPlace(*field);
    }
    if(const auto *index = dynamic_cast<const rx::ast::IndexExpr *>(&expression)){
        return generateIndexPlace(*index);
    }
    throw std::runtime_error("IR place expression is not implemented");
}

IRExprResult IRGenerator::generateAssignment(const rx::ast::AssignExpr &expression){
    const auto value = generateCoercedExpr(expression.value());
    if(value.terminated) return value;  //如果生成右侧时已经终结当前基本块，就直接返回，不能继续生成赋值指令 eg. return

    const auto place = generatePlace(expression.target());
    if(!place) return {{}, true};

    context_.builder.createStore(materializeValue(value, place->type), place->address);
    return {};
}

std::optional<IRPlace> IRGenerator::generateFieldPlace(const rx::ast::FieldExpr &expression){
    const auto &model = *context_.model;
    auto &builder = context_.builder;
    const auto &base = expression.base();

    const auto result = generatePlaceOrTemporary(base);
    if(!result) return std::nullopt;
    IRPlace place = *result;

    // place.address 指向存储引用的槽；load 后得到引用指向的地址。
    while(model.types.get(place.type).kind() == TypeKind::Reference || model.types.get(place.type).kind() == TypeKind::Box){
        const TypeId elementType = model.types.get(place.type).elementType();
        place.address = builder.createLoad(IRType::Ptr, place.address);
        place.type = elementType;
    }

    const auto &owner = model.types.get(place.type);
    if(owner.kind() != TypeKind::Struct){
        throw std::logic_error("IR field base is not a struct");
    }

    const auto &layout = context_.structs.at(owner.symbol());
    if(!layout.structure){
        throw std::logic_error("IR struct layout is missing");
    }

    const std::size_t fieldIndex = layout.fieldIndices.at(expression.field());
    const auto address = builder.createFieldAddress(*layout.structure, place.address, fieldIndex);

    return IRPlace{
        address,
        model.expressionTypes.at(expression.id())
    };
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
    const IRType type = context_.typeLowering.lowerStorageType(place->type, model);
    IRValue right = *value.value;
    const auto &rightInfo = model.types.get(model.expressionTypes.at(expression.value().id()));
    // 复合赋值的右侧允许一层共享引用，先读取其指向的标量。
    if(rightInfo.kind() == TypeKind::Reference && !rightInfo.isMutable()){
        right = context_.builder.createLoad(context_.typeLowering.lowerStorageType(rightInfo.elementType(), model), right);
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
        const auto stored = materializeValue(value, model.expressionTypes.at(operand.id()));
        const auto address = context_.builder.createEntryAlloca(stored.type);
        context_.builder.createStore(stored, address);
        return {address, false};
    }

    if(op == "*"){
        const auto place = generatePlace(expression);
        if(!place) return {{}, true};

        const IRType type = context_.typeLowering.lowerStorageType(place->type, model);
        if(type == IRType::Unit) return {};

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
        const IRType type = context_.typeLowering.lowerStorageType(operandType.elementType(), model);
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
