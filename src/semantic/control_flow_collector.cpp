#include "control_flow_collector.hpp"
#include "semantic_checker.hpp"

TypeId ControlFlowCollector::collectIf(const rx::ast::IfExpr &ifstmt, std::optional<TypeId> expectedType){
    // 要求条件为 bool，检查各分支，并按预期类型或 LUB 确定整个 if 的类型
    const TypeId errorType = checker_.model_.types.builtin(TypeKind::Error);
    const TypeId neverType = checker_.model_.types.builtin(TypeKind::Never);
    const TypeId unitType = checker_.model_.types.builtin(TypeKind::Unit);
    checker_.model_.expressionInfo.insert_or_assign(ifstmt.id(), ExprInfo{false, false});
    if(expectedType == errorType)  expectedType.reset();

    const TypeId condition = checker_.expressionCollector_.collectExpr(ifstmt.condition(), checker_.model_.types.builtin(TypeKind::Bool));
    const std::optional<TypeId> branchExpected = ifstmt.elseBranch() != nullptr ? expectedType : unitType;
    const TypeId thenType = checker_.expressionCollector_.collectExpr(ifstmt.thenBranch(), expectedType);
    //没有else默认else分支返回的是unit()
    TypeId elseType = unitType;
    if(ifstmt.elseBranch() != nullptr){
        elseType = checker_.expressionCollector_.collectExpr(*ifstmt.elseBranch(), expectedType);
    }

    if(condition == errorType || thenType == errorType || elseType == errorType) return errorType;

    // 条件是否发散不改变按分支确定的静态类型
    if(ifstmt.elseBranch() == nullptr) return unitType;
    if(thenType == neverType && elseType == neverType) return neverType;
    if(expectedType.has_value()) return *expectedType;
    //LUB
    return checker_.typesChecker_.collectCommonType({thenType, elseType}, ifstmt);
}

TypeId ControlFlowCollector::collectLoop(const rx::ast::LoopExpr &loop, std::optional<TypeId> expectedType){
    // 建立循环上下文，收集本层 break 值并确定循环结果类型；无正常出口时可为 never
    const TypeId errorType = checker_.model_.types.builtin(TypeKind::Error);
    const TypeId unitType = checker_.model_.types.builtin(TypeKind::Unit);
    const TypeId neverType = checker_.model_.types.builtin(TypeKind::Never);
    checker_.model_.expressionInfo.insert_or_assign(loop.id(), ExprInfo{false, false});
    if(expectedType == errorType) expectedType.reset();

    LoopContext context{LoopKind::Loop, expectedType, {}};
    LoopGuard loopScope(currentLoop_, context);

    const TypeId bodyType = checker_.expressionCollector_.collectExpr(loop.body(), unitType);
    if(bodyType == errorType || !context.valid) return errorType;

    if(expectedType.has_value()){
        for(TypeId type : context.breakTypes){
            if(type != neverType) return *expectedType;
        }
        return neverType;
    }

    // 空集合和全部为 never 的 break 值都得到 never
    return checker_.typesChecker_.collectCommonType(context.breakTypes, loop);
}

TypeId ControlFlowCollector::collectWhile(const rx::ast::WhileExpr &loop){
    // 要求条件为 bool，在循环上下文中检查 unit 循环体以及 break 和 continue
    const TypeId errorType = checker_.model_.types.builtin(TypeKind::Error);
    const TypeId unitType = checker_.model_.types.builtin(TypeKind::Unit);
    checker_.model_.expressionInfo.insert_or_assign(loop.id(), ExprInfo{false, false});

    TypeId condition;
    {
        // 条件中的跳转只能指向条件内部新建立的循环。
        LoopContext context{LoopKind::Condition, std::nullopt, {}};
        LoopGuard conditionScope(currentLoop_, context);

        condition = checker_.expressionCollector_.collectExpr(
            loop.condition(), checker_.model_.types.builtin(TypeKind::Bool)
        );
    }

    LoopContext context{LoopKind::While, std::nullopt, {}};
    LoopGuard loopScope(currentLoop_, context);

    const TypeId bodyType = checker_.expressionCollector_.collectExpr(loop.body(), unitType);
    if(condition == errorType || bodyType == errorType || !context.valid) return errorType;

    // 即使条件恒为 true 或发散，while 的静态类型仍为 unit
    return unitType;
}

TypeId ControlFlowCollector::collectReturn(const rx::ast::ReturnExpr &returnstmt){
    // 以当前函数返回类型检查返回值或 bare return，并将表达式类型记为 never
    const TypeId errorType = checker_.model_.types.builtin(TypeKind::Error);
    const TypeId unitType = checker_.model_.types.builtin(TypeKind::Unit);
    checker_.model_.expressionInfo.insert_or_assign(returnstmt.id(), ExprInfo{false, false});
    const auto expectedType = checker_.currentReturnType_;

    TypeId valueType = unitType;
    if(returnstmt.value() != nullptr){
        valueType = checker_.expressionCollector_.collectExpr(*returnstmt.value(), expectedType);
    }

    if(!expectedType.has_value()){
        checker_.diagnose(returnstmt, "return outside a function");
        return errorType;
    }
    if(*expectedType == errorType || valueType == errorType) return errorType;

    if(returnstmt.value() == nullptr && !checker_.typesChecker_.canCoerce(unitType, *expectedType)){
        checker_.diagnose(returnstmt, "bare return requires a unit return type");
        return errorType;
    }

    return checker_.model_.types.builtin(TypeKind::Never);

}

TypeId ControlFlowCollector::collectBreak(const rx::ast::BreakExpr &breakstmt){
    // 确认位于循环内，把值加入最近循环的结果候选，并将表达式类型记为 never。
    const TypeId errorType = checker_.model_.types.builtin(TypeKind::Error);
    const TypeId unitType = checker_.model_.types.builtin(TypeKind::Unit);
    checker_.model_.expressionInfo.insert_or_assign(breakstmt.id(), ExprInfo{false, false});

    LoopContext *target = currentLoop_;
    const bool valueLoop = target != nullptr && target->kind == LoopKind::Loop;

    // 先占据源码位置，避免操作数内部的 break 被排到当前 break 前面。
    std::size_t position = 0;
    if(valueLoop){
        position = target->breakTypes.size();
        target->breakTypes.push_back(errorType);
    }

    const std::optional<TypeId> expectedType = valueLoop ? target->expectedType : std::nullopt;
    TypeId valueType = unitType;
    if(breakstmt.value() != nullptr){
        valueType = checker_.expressionCollector_.collectExpr(*breakstmt.value(), expectedType);
    }

    if(target == nullptr || target->kind == LoopKind::Condition){
        checker_.diagnose(breakstmt, "break has no enclosing loop in this context");
        return errorType;
    }

    if(target->kind == LoopKind::While && breakstmt.value() != nullptr){
        checker_.diagnose(breakstmt, "break with a value requires a loop target");
        target->valid = false;
        return errorType;
    }

    if(valueType == errorType){
        target->valid = false;
        return errorType;
    }

    if(breakstmt.value() == nullptr && expectedType.has_value() &&
       !checker_.typesChecker_.canCoerce(unitType, *expectedType)){
        checker_.diagnose(breakstmt, "bare break does not match the expected loop type");
        target->valid = false;
        return errorType;
    }

    if(valueLoop) target->breakTypes[position] = valueType;
    return checker_.model_.types.builtin(TypeKind::Never);
}

TypeId ControlFlowCollector::collectContinue(const rx::ast::ContinueExpr &continuestmt){
    // 确认位于循环内且无需结果值，并将表达式类型记为 never
    const TypeId errorType = checker_.model_.types.builtin(TypeKind::Error);
    checker_.model_.expressionInfo.insert_or_assign(continuestmt.id(), ExprInfo{false, false});

    if(currentLoop_ == nullptr || currentLoop_->kind == LoopKind::Condition){
        checker_.diagnose(continuestmt, "continue has no enclosing loop in this context");
        return errorType;
    }

    return checker_.model_.types.builtin(TypeKind::Never);
}
