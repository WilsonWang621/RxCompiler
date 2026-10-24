#include "expression_collector.hpp"
#include "semantic_checker.hpp"

#include <limits>

TypeId ExpressionCollector::collectExpr(const rx::ast::Expr &expression, std::optional<TypeId> expectedType){
    // 按具体 AST 类型分派检查，记录 expressionTypes 和 place、可变性等 ExprInfo。
    const TypeId errorType = checker_.model_.types.builtin(TypeKind::Error);
    if(expectedType.has_value()){
        checker_.model_.types.get(*expectedType);
        // 预期类型已出错时仍检查表达式，但不再施加该类型约束。
        if(*expectedType == errorType) expectedType.reset();
    }
    checker_.model_.expressionInfo.insert_or_assign(expression.id(), ExprInfo{false, false});
    TypeId result = errorType;
    if(const auto *unit = dynamic_cast<const rx::ast::UnitExpr *>(&expression)){
        result = collectUnit(*unit);
    }else if(const auto *integer = dynamic_cast<const rx::ast::IntegerLiteralExpr *>(&expression)){
        result = collectIntegerLiteral(*integer, expectedType);
    }else if(const auto *boolean = dynamic_cast<const rx::ast::BooleanLiteralExpr *>(&expression)){
        result = collectBooleanLiteral(*boolean);
    }else if(const auto *path = dynamic_cast<const rx::ast::PathExpr *>(&expression)){
        result = collectPath(*path);
    }else if(const auto *binary = dynamic_cast<const rx::ast::BinaryExpr *>(&expression)){
        result = collectBinaryExpr(*binary);
    }else if(const auto *unary = dynamic_cast<const rx::ast::UnaryExpr *>(&expression)){
        result = collectUnaryExpr(*unary);
    }else if(const auto *cast = dynamic_cast<const rx::ast::CastExpr *>(&expression)){
        result = collectCast(*cast);
    }else if(const auto *assignment = dynamic_cast<const rx::ast::AssignExpr *>(&expression)){
        result = collectAssignment(*assignment);
    }else if(const auto *assignment = dynamic_cast<const rx::ast::CompoundAssignExpr *>(&expression)){
        result = collectCompoundAssignment(*assignment);
    }else if(const auto *array = dynamic_cast<const rx::ast::ArrayExpr *>(&expression)){
        result = collectArray(*array, expectedType);
    }else if(const auto *array = dynamic_cast<const rx::ast::ArrayRepeatExpr *>(&expression)){
        result = collectRepeatedArray(*array, expectedType);
    }else if(const auto *index = dynamic_cast<const rx::ast::IndexExpr *>(&expression)){
        result = collectIndex(*index);
    }else if(const auto *structure = dynamic_cast<const rx::ast::StructExpr *>(&expression)){
        result = collectStructExpr(*structure);
    }else if(const auto *field = dynamic_cast<const rx::ast::FieldExpr *>(&expression)){
        result = collectField(*field);
    }else if(const auto *call = dynamic_cast<const rx::ast::CallExpr *>(&expression)){
        result = collectCall(*call);
    }else if(const auto *call = dynamic_cast<const rx::ast::MethodCallExpr *>(&expression)){
        result = collectMethodCall(*call);
    }else if(const auto *block = dynamic_cast<const rx::ast::BlockExpr *>(&expression)){
        // 普通嵌套块需要新作用域，不能直接调用 collectBlockContents。
        result = checker_.statementCollector_.collectFunctionBlock(*block, expectedType);
    }else if(const auto *ifstmt = dynamic_cast<const rx::ast::IfExpr *>(&expression)){
        result = checker_.controlFlowCollector_.collectIf(*ifstmt, expectedType);
    }else if(const auto *loop = dynamic_cast<const rx::ast::LoopExpr *>(&expression)){
        result = checker_.controlFlowCollector_.collectLoop(*loop, expectedType);
    }else if(const auto *loop = dynamic_cast<const rx::ast::WhileExpr *>(&expression)){
        result = checker_.controlFlowCollector_.collectWhile(*loop);
    }else if(const auto *returnstmt = dynamic_cast<const rx::ast::ReturnExpr *>(&expression)){
        result = checker_.controlFlowCollector_.collectReturn(*returnstmt);
    }else if(const auto *breakstmt = dynamic_cast<const rx::ast::BreakExpr *>(&expression)){
        result = checker_.controlFlowCollector_.collectBreak(*breakstmt);
    }else if(const auto *continuestmt = dynamic_cast<const rx::ast::ContinueExpr *>(&expression)){
        result = checker_.controlFlowCollector_.collectContinue(*continuestmt);
    }else{
        checker_.diagnose(expression, "unsupported expression");
    }

    checker_.model_.types.get(result);
    if(result != errorType && expectedType.has_value() && !checker_.typesChecker_.canCoerce(result, *expectedType)){
        checker_.diagnose(expression, "expression type does not match expected type");
        result = errorType;
    }

    checker_.model_.expressionTypes.insert_or_assign(expression.id(), result);
    if(result == errorType){
        checker_.model_.expressionInfo.insert_or_assign(expression.id(), ExprInfo{false, false});
    }
    return result;
}

TypeId ExpressionCollector::collectUnit(const rx::ast::UnitExpr &expression){
    // 将 unit 表达式标记为非 place，并返回内建 Unit 类型。
    checker_.model_.expressionInfo.insert_or_assign(expression.id(), ExprInfo{false, false});
    return checker_.model_.types.builtin(TypeKind::Unit);
}

TypeId ExpressionCollector::collectIntegerLiteral(const rx::ast::IntegerLiteralExpr &expression, std::optional<TypeId> expectedType){
    // 解析进制、下划线和后缀，按后缀、预期整数类型、i32 默认值的顺序确定类型。
    checker_.model_.expressionInfo.insert_or_assign(expression.id(), ExprInfo{false, false});
    //先提供默认类型，再考虑预期类型：
    TypeKind kind = TypeKind::I32;
    if(expectedType.has_value()){
        const auto &expected = checker_.model_.types.get(*expectedType);
        if(expected.isInteger()) kind = expected.kind();
    }
    //检查显式后缀，并覆盖之前的选择：
    const std::string &text = expression.text();
    for(const auto &[suffix, suffixKind] : std::initializer_list<std::pair<std::string, TypeKind>>{
        {"i32", TypeKind::I32}, {"u32", TypeKind::U32}, {"isize", TypeKind::Isize}, {"usize", TypeKind::Usize}}){
        if(text.size() >= suffix.size() && text.compare(text.size() - suffix.size(), suffix.size(), suffix) == 0){
            kind = suffixKind;
            break;
        }
    }

    const TypeId type = checker_.model_.types.builtin(kind);
    const auto value = checker_.constEvaluate_.evaluateConstantExpr(expression, type);
    if(!value.has_value()) return checker_.model_.types.builtin(TypeKind::Error);
    return value->type;
}

TypeId ExpressionCollector::collectBooleanLiteral(const rx::ast::BooleanLiteralExpr &expression){
    // 将布尔字面量标记为非 place，并返回内建 Bool 类型。
    const TypeId type = checker_.model_.types.builtin(TypeKind::Bool);
    checker_.model_.expressionInfo.insert_or_assign(expression.id(), ExprInfo{false, false});
    checker_.model_.constantValues.insert_or_assign(
        expression.id(), ConstValue{type, expression.value() ? 1u : 0u}
    );
    return type;
}

TypeId ExpressionCollector::collectPath(const rx::ast::PathExpr &path){
    // 解析路径符号，取得常量或绑定的类型，并记录该表达式是否为可变 place。
    const TypeId errorType = checker_.model_.types.builtin(TypeKind::Error);
    checker_.model_.expressionInfo.insert_or_assign(path.id(), ExprInfo{false, false});
    const auto symbol = checker_.resolution_.resolveValuePath(path);
    if(!symbol.has_value()) return errorType;

    const auto &info = checker_.model_.symbols.at(*symbol);
    if(info.kind == SymbolKind::Constant){
        const auto value = checker_.constEvaluate_.evaluateConstant(*symbol);
        if(!value.has_value()) return errorType;
        checker_.model_.constantValues.insert_or_assign(path.id(), *value);
        return value->type;
    }
    if(info.kind == SymbolKind::Local || info.kind == SymbolKind::Parameter || info.kind == SymbolKind::Receiver){
        const TypeId type = checker_.model_.symbolTypes.at(*symbol);
        checker_.model_.types.get(type);
        checker_.model_.expressionInfo.insert_or_assign(path.id(), ExprInfo{info.isMutable, true});
        return type;
    }

    checker_.diagnose(path, "path does not name a constant or binding");
    return errorType;
}

TypeId ExpressionCollector::collectBinaryExpr(const rx::ast::BinaryExpr &expression){
    // 按运算符检查左右操作数类型、短路规则和比较能力，确定运算结果类型。
    const TypeId errorType = checker_.model_.types.builtin(TypeKind::Error);
    const TypeId neverType = checker_.model_.types.builtin(TypeKind::Never);
    const TypeId boolType = checker_.model_.types.builtin(TypeKind::Bool);
    checker_.model_.expressionInfo.insert_or_assign(expression.id(), ExprInfo{false, false});

    // 两侧都要做语义检查；短路只影响后续生成的运行时代码。
    const TypeId left = collectExpr(expression.left());
    const TypeId right = collectExpr(expression.right());
    if(left == errorType || right == errorType) return errorType;

    const std::string &op = expression.op();
    const bool logical = op == "&&" || op == "||";
    const bool equality = op == "==" || op == "!=";
    const bool ordering = op == "<" || op == "<=" || op == ">" || op == ">=";
    const bool arithmetic = op == "+" || op == "-" || op == "*" || op == "/" || op == "%";
    const bool bitwise = op == "&" || op == "|" || op == "^";
    const bool shift = op == "<<" || op == ">>";
    if(!logical && !equality && !ordering && !arithmetic && !bitwise && !shift){
        checker_.diagnose(expression, "unsupported binary operator '" + op + "'");
        return errorType;
    }

    if(logical){
        if(!checker_.typesChecker_.canCoerce(left, boolType) || !checker_.typesChecker_.canCoerce(right, boolType)){
            checker_.diagnose(expression, "logical operators require bool operands");
            return errorType;
        }
        return left == neverType ? neverType : boolType;
    }
    if(left == neverType && right == neverType) return neverType;

    const bool diverges = left == neverType || right == neverType;
    TypeId leftType = left == neverType ? right : left;
    TypeId rightType = right == neverType ? left : right;
    bool valid = false;
    TypeId result = boolType;

    if(equality){
        valid = leftType == rightType && checker_.typesChecker_.hasPartialEq(leftType);
    }else if(ordering){
        const auto &leftInfo = checker_.model_.types.get(leftType);
        const auto &rightInfo = checker_.model_.types.get(rightType);
        // 只允许最外层 &T 与 &mut T 的这个方向调整。
        if(leftInfo.kind() == TypeKind::Reference && !leftInfo.isMutable() &&
           rightInfo.kind() == TypeKind::Reference && rightInfo.isMutable() &&
           leftInfo.elementType() == rightInfo.elementType()){
            rightType = leftType;
        }
        while(checker_.model_.types.get(leftType).kind() == TypeKind::Reference &&
              checker_.model_.types.get(rightType).kind() == TypeKind::Reference){
            const auto &leftReference = checker_.model_.types.get(leftType);
            const auto &rightReference = checker_.model_.types.get(rightType);
            if(leftReference.isMutable() != rightReference.isMutable()) break;
            leftType = leftReference.elementType();
            rightType = rightReference.elementType();
        }
        const auto &scalar = checker_.model_.types.get(leftType);
        valid = leftType == rightType && (scalar.isInteger() || scalar.kind() == TypeKind::Bool);
    }else{
        // 算术、位运算和移位只支持一层共享引用，不自动解引用 Box。
        const auto &leftInfo = checker_.model_.types.get(leftType);
        const auto &rightInfo = checker_.model_.types.get(rightType);
        if(leftInfo.kind() == TypeKind::Reference && !leftInfo.isMutable()) leftType = leftInfo.elementType();
        if(rightInfo.kind() == TypeKind::Reference && !rightInfo.isMutable()) rightType = rightInfo.elementType();
        const auto &leftScalar = checker_.model_.types.get(leftType);
        const auto &rightScalar = checker_.model_.types.get(rightType);
        if(shift){
            valid = leftScalar.isInteger() && rightScalar.isInteger();
        }else{
            valid = leftType == rightType &&
                    (leftScalar.isInteger() || (bitwise && leftScalar.kind() == TypeKind::Bool));
        }
        result = leftType;
    }

    if(!valid){
        checker_.diagnose(expression, "invalid operands for binary operator '" + op + "'");
        return errorType;
    }
    return diverges ? neverType : result;
}

TypeId ExpressionCollector::collectUnaryExpr(const rx::ast::UnaryExpr &expression){
    // 检查负号、逻辑非、借用和解引用的操作数约束，并计算结果类型及 place 属性。
    const TypeId errorType = checker_.model_.types.builtin(TypeKind::Error);
    const TypeId neverType = checker_.model_.types.builtin(TypeKind::Never);
    checker_.model_.expressionInfo.insert_or_assign(expression.id(), ExprInfo{false, false});
    const std::string &op = expression.op();

    // 整体求值负数字面量，允许 -2147483648i32 等有符号最小值。
    if(op == "-"){
        if(const auto *literal = dynamic_cast<const rx::ast::IntegerLiteralExpr *>(&expression.operand())){
            const std::string &text = literal->text();
            const bool isIsize = text.size() >= 5 && text.compare(text.size() - 5, 5, "isize") == 0;
            const TypeId type = checker_.model_.types.builtin(isIsize ? TypeKind::Isize : TypeKind::I32);
            checker_.model_.expressionInfo.insert_or_assign(literal->id(), ExprInfo{false, false});
            const auto value = checker_.constEvaluate_.evaluateConstantExpr(expression, type);
            return value.has_value() ? value->type : errorType;
        }
    }

    const TypeId type = collectExpr(expression.operand());
    if(type == errorType || type == neverType) return type;
    const auto &info = checker_.model_.types.get(type);
    const auto &operandInfo = checker_.model_.expressionInfo.at(expression.operand().id());

    if(op == "&" || op == "&mut"){
        const bool isMutable = op == "&mut";
        if(isMutable && operandInfo.isPlace && !checkMutablePlace(expression.operand())) return errorType;
        // 值表达式可物化成临时存储，允许对临时值做可变借用。
        return checker_.model_.types.reference(type, isMutable);
    }
    if(op == "*"){
        if(info.kind() != TypeKind::Reference && info.kind() != TypeKind::Box){
            checker_.diagnose(expression, "dereference requires a reference or Box");
            return errorType;
        }
        bool isMutable = info.kind() == TypeKind::Reference ? info.isMutable() :
                         (!operandInfo.isPlace || operandInfo.mut);
        bool sharedAccess = false;
        const rx::ast::Expr *current = &expression.operand();

        // 回溯已有访问路径，区分不可变绑定和共享引用造成的只读访问。
        while(current != nullptr && !sharedAccess &&
              checker_.model_.expressionInfo.at(current->id()).isPlace){
            const rx::ast::Expr *base = nullptr;
            bool explicitDeref = false;
            bool indexing = false;
            if(const auto *unary = dynamic_cast<const rx::ast::UnaryExpr *>(current)){
                if(unary->op() != "*") break;
                base = &unary->operand();
                explicitDeref = true;
            }else if(const auto *field = dynamic_cast<const rx::ast::FieldExpr *>(current)){
                base = &field->base();
            }else if(const auto *index = dynamic_cast<const rx::ast::IndexExpr *>(current)){
                base = &index->base();
                indexing = true;
            }else{
                break;
            }

            TypeId baseType = checker_.model_.expressionTypes.at(base->id());
            while(true){
                const auto &baseInfo = checker_.model_.types.get(baseType);
                if(baseInfo.kind() == TypeKind::Reference && !baseInfo.isMutable()){
                    sharedAccess = true;
                    break;
                }
                if(explicitDeref || (baseInfo.kind() != TypeKind::Reference && baseInfo.kind() != TypeKind::Box)) break;
                baseType = baseInfo.elementType();
            }
            // Vec 索引隐式借用容器；共享索引访问同样阻断后续可变解引用。
            if(indexing && checker_.model_.types.get(baseType).kind() == TypeKind::Vec &&
               !checker_.model_.expressionInfo.at(current->id()).mut){
                sharedAccess = true;
            }
            current = base;
        }
        checker_.model_.expressionInfo.insert_or_assign(expression.id(), ExprInfo{isMutable && !sharedAccess, true});
        return info.elementType();
    }

    TypeId scalarType = type;
    if(info.kind() == TypeKind::Reference && !info.isMutable()) scalarType = info.elementType();
    const auto &scalar = checker_.model_.types.get(scalarType);
    if((op == "-" && scalar.isSignedInteger()) ||
       (op == "!" && (scalar.isInteger() || scalar.kind() == TypeKind::Bool))){
        return scalarType;
    }
    checker_.diagnose(expression, "invalid operand for unary operator '" + op + "'");
    return errorType;
}

TypeId ExpressionCollector::collectCast(const rx::ast::CastExpr &expression){
    // 解析目标类型，检查源类型是否允许显式 as 转换，并返回目标类型。
    TypeId source = collectExpr(expression.value());
    TypeId target = checker_.resolution_.resolveType(expression.type());

    TypeId errorType = checker_.model_.types.builtin(TypeKind::Error);
    if(source == errorType || target == errorType){
        return errorType;
    }

    if(!checker_.typesChecker_.canCast(source, target)){
        checker_.diagnose(expression, "invalid cast");
        return errorType;
    }
    return target;
}

TypeId ExpressionCollector::collectAssignment(const rx::ast::AssignExpr &assignment){
    // 检查左侧为可变 place，以其类型检查右侧的求值和隐式转换，结果为 unit。
    const TypeId errorType = checker_.model_.types.builtin(TypeKind::Error);
    checker_.model_.expressionInfo.insert_or_assign(assignment.id(), ExprInfo{false, false});

    // 这里按类型依赖先检查目标；运行时代码仍需先求右值，再计算目标地址。
    const TypeId targetType = collectExpr(assignment.target());
    const bool mutableTarget = checkMutablePlace(assignment.target());
    const TypeId valueType = collectExpr(assignment.value(), targetType);
    if(targetType == errorType || valueType == errorType || !mutableTarget) return errorType;
    return checker_.model_.types.builtin(TypeKind::Unit);
}

TypeId ExpressionCollector::collectCompoundAssignment(const rx::ast::CompoundAssignExpr &assignment){
    // 检查目标可变性及对应二元运算约束，保证目标只求值一次，结果为 unit。
    const TypeId errorType = checker_.model_.types.builtin(TypeKind::Error);
    const TypeId neverType = checker_.model_.types.builtin(TypeKind::Never);
    checker_.model_.expressionInfo.insert_or_assign(assignment.id(), ExprInfo{false, false});

    const TypeId targetType = collectExpr(assignment.target());
    const bool mutableTarget = checkMutablePlace(assignment.target());
    // 复合赋值不使用目标类型推导右侧整数，也不改写成重复目标的二元表达式。
    const TypeId valueType = collectExpr(assignment.value());
    if(targetType == errorType || valueType == errorType || !mutableTarget) return errorType;

    const std::string &op = assignment.op();
    const bool arithmetic = op == "+=" || op == "-=" || op == "*=" || op == "/=" || op == "%=";
    const bool bitwise = op == "&=" || op == "|=" || op == "^=";
    const bool shift = op == "<<=" || op == ">>=";
    if(!arithmetic && !bitwise && !shift){
        checker_.diagnose(assignment, "unsupported compound assignment operator '" + op + "'");
        return errorType;
    }

    // 左侧必须直接是原始标量 place；右侧允许一层共享引用。
    TypeId scalarType = valueType == neverType ? targetType : valueType;
    const auto &value = checker_.model_.types.get(scalarType);
    if(value.kind() == TypeKind::Reference && !value.isMutable()) scalarType = value.elementType();
    const auto &target = checker_.model_.types.get(targetType);
    const auto &scalar = checker_.model_.types.get(scalarType);

    bool valid = false;
    if(shift){
        valid = target.isInteger() && scalar.isInteger();
    }else{
        valid = targetType == scalarType && (target.isInteger() || (bitwise && target.kind() == TypeKind::Bool));
    }
    if(!valid){
        checker_.diagnose(assignment, "invalid operands for compound assignment operator '" + op + "'");
        return errorType;
    }
    return checker_.model_.types.builtin(TypeKind::Unit);
}

TypeId ExpressionCollector::collectArray(const rx::ast::ArrayExpr &array, std::optional<TypeId> expectedType){
    const TypeId errorType = checker_.model_.types.builtin(TypeKind::Error);
    checker_.model_.expressionInfo.insert_or_assign(array.id(), ExprInfo{false, false});
    std::optional<TypeId> expectedElement;
    std::optional<std::uint32_t> expectedLength;
    if(expectedType.has_value()){
        const auto &expected = checker_.model_.types.get(*expectedType);
        if(expected.kind() == TypeKind::Array){
            expectedElement = expected.elementType();
            expectedLength = expected.length();
        }
    }

    std::vector<TypeId> elements;
    elements.reserve(array.elements().size());
    bool valid = true;
    for(const auto &element : array.elements()){
        const TypeId type = collectExpr(*element, expectedElement);
        elements.push_back(type);
        if(type == errorType) valid = false;
    }

    if(array.elements().size() > std::numeric_limits<std::uint32_t>::max()){
        checker_.diagnose(array, "array has too many elements");
        return errorType;
    }
    const auto length = static_cast<std::uint32_t>(array.elements().size());
    if(expectedLength.has_value() && *expectedLength != length){
        checker_.diagnose(array, "array length does not match expected type");
        valid = false;
    }
    if(!valid) return errorType;
    if(elements.empty() && !expectedElement.has_value()){
        checker_.diagnose(array, "cannot infer element type of an empty array");
        return errorType;
    }

    // 有预期类型时采用转换目标；否则在各元素独立定型后计算 LUB。
    const TypeId elementType = expectedElement.has_value() ? *expectedElement : checker_.typesChecker_.collectCommonType(elements, array);
    if(elementType == errorType) return errorType;
    return checker_.model_.types.array(elementType, length);
}

TypeId ExpressionCollector::collectRepeatedArray(const rx::ast::ArrayRepeatExpr &repeatedArray, std::optional<TypeId> expectedType){
    const TypeId errorType = checker_.model_.types.builtin(TypeKind::Error);
    checker_.model_.expressionInfo.insert_or_assign(repeatedArray.id(), ExprInfo{false, false});
    std::optional<TypeId> expectedElement;
    std::optional<std::uint32_t> expectedLength;
    if(expectedType.has_value()){
        const auto &expected = checker_.model_.types.get(*expectedType);
        if(expected.kind() == TypeKind::Array){
            expectedElement = expected.elementType();
            expectedLength = expected.length();
        }
    }

    // 只有长度是常量上下文，元素仍按普通表达式检查一次。
    const auto length = checker_.constEvaluate_.evaluateArrayLength(repeatedArray.count());
    const TypeId valueType = collectExpr(repeatedArray.value(), expectedElement);
    bool valid = length.has_value() && valueType != errorType;
    if(length.has_value() && expectedLength.has_value() && *length != *expectedLength){
        checker_.diagnose(repeatedArray, "array length does not match expected type");
        valid = false;
    }
    if(!valid) return errorType;

    const TypeId elementType = expectedElement.has_value() ? *expectedElement : valueType;
    if(elementType == errorType) return errorType;
    if(*length > 1 && !checker_.typesChecker_.hasCopy(elementType)){
        checker_.diagnose(repeatedArray.value(), "repeating an element more than once requires Copy");
        return errorType;
    }
    return checker_.model_.types.array(elementType, *length);
}

TypeId ExpressionCollector::collectIndex(const rx::ast::IndexExpr &expression){
    const TypeId errorType = checker_.model_.types.builtin(TypeKind::Error);
    const TypeId neverType = checker_.model_.types.builtin(TypeKind::Never);
    const TypeId usizeType = checker_.model_.types.builtin(TypeKind::Usize);
    checker_.model_.expressionInfo.insert_or_assign(expression.id(), ExprInfo{false, false});

    const TypeId baseType = collectExpr(expression.base());
    const TypeId indexType = collectExpr(expression.index(), usizeType);
    if(baseType == errorType || indexType == errorType) return errorType;
    if(indexType != usizeType){
        checker_.diagnose(expression.index(), "index must have type usize");
        return errorType;
    }
    if(baseType == neverType) return neverType;

    bool sharedAccess = false;
    const rx::ast::Expr *current = &expression.base();
    // 回溯显式解引用、字段及先前索引，保留共享访问造成的限制。
    while(current != nullptr && !sharedAccess &&
          checker_.model_.expressionInfo.at(current->id()).isPlace){
        const rx::ast::Expr *base = nullptr;
        bool explicitDeref = false;
        bool indexing = false;
        if(const auto *unary = dynamic_cast<const rx::ast::UnaryExpr *>(current)){
            if(unary->op() != "*") break;
            base = &unary->operand();
            explicitDeref = true;
        }else if(const auto *field = dynamic_cast<const rx::ast::FieldExpr *>(current)){
            base = &field->base();
        }else if(const auto *index = dynamic_cast<const rx::ast::IndexExpr *>(current)){
            base = &index->base();
            indexing = true;
        }else{
            break;
        }

        TypeId ownerType = checker_.model_.expressionTypes.at(base->id());
        while(true){
            const auto &owner = checker_.model_.types.get(ownerType);
            if(owner.kind() == TypeKind::Reference && !owner.isMutable()){
                sharedAccess = true;
                break;
            }
            if(explicitDeref || (owner.kind() != TypeKind::Reference && owner.kind() != TypeKind::Box)) break;
            ownerType = owner.elementType();
        }
        if(indexing && checker_.model_.types.get(ownerType).kind() == TypeKind::Vec &&
           !checker_.model_.expressionInfo.at(current->id()).mut){
            sharedAccess = true;
        }
        current = base;
    }

    const auto &baseInfo = checker_.model_.expressionInfo.at(expression.base().id());
    bool isMutable = (!baseInfo.isPlace || baseInfo.mut) && !sharedAccess;
    TypeId containerType = baseType;
    while(true){
        const auto &container = checker_.model_.types.get(containerType);
        if(container.kind() != TypeKind::Reference && container.kind() != TypeKind::Box) break;
        if(container.kind() == TypeKind::Reference){
            if(!container.isMutable()) sharedAccess = true;
            isMutable = container.isMutable() && !sharedAccess;
        }
        containerType = container.elementType();
    }

    const auto &container = checker_.model_.types.get(containerType);
    if(container.kind() != TypeKind::Array && container.kind() != TypeKind::Vec){
        checker_.diagnose(expression.base(), "expression is not indexable");
        return errorType;
    }
    checker_.model_.expressionInfo.insert_or_assign(expression.id(), ExprInfo{isMutable, true});
    return container.elementType();
}

TypeId ExpressionCollector::collectStructExpr(const rx::ast::StructExpr &expression){
    // 解析结构体，检查字段无重复、无遗漏且初始化值匹配声明类型，返回结构体类型。
}

TypeId ExpressionCollector::collectField(const rx::ast::FieldExpr &expression){
    // 根据基表达式类型查找字段，返回字段类型并传播基 place 的可变访问能力。
}

TypeId ExpressionCollector::collectCall(const rx::ast::CallExpr &expression){
    // 解析调用目标，按函数签名检查实参数量、求值顺序和类型，返回函数结果类型。
}

TypeId ExpressionCollector::collectMethodCall(const rx::ast::MethodCallExpr &expression){
    // 根据接收者类型查找方法，执行允许的自动借用或解引用，并检查其余实参。
}

bool ExpressionCollector::checkMutablePlace(const rx::ast::Expr &expression){
    // 查询表达式 place 信息，诊断不可赋值或不可变目标，并报告是否允许写入。
    const auto type = checker_.model_.expressionTypes.find(expression.id());
    if(type != checker_.model_.expressionTypes.end() && type->second == checker_.model_.types.builtin(TypeKind::Error)){
        return false;
    }

    const auto info = checker_.model_.expressionInfo.find(expression.id());
    if(info == checker_.model_.expressionInfo.end() || !info->second.isPlace){
        checker_.diagnose(expression, "expression is not an assignable place");
        return false;
    }
    if(!info->second.mut){
        checker_.diagnose(expression, "cannot modify an immutable place");
        return false;
    }
    return true;
}
