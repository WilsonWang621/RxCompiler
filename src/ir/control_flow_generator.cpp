#include "ir_generator.hpp"

#include <stdexcept>

IRExprResult IRGenerator::generateReturn(const rx::ast::ReturnExpr &expression){
    const auto result = expression.value() ? generateCoercedExpr(*expression.value()) : IRExprResult{};
    if(!result.terminated) context_.builder.createReturn(result.value);
    return {{}, true};
}

IRExprResult IRGenerator::generateLogical(const rx::ast::BinaryExpr &expression){
    const auto left = generateExpr(expression.left());
    if(left.terminated) return left;
    if(!left.value || left.value->type != IRType::I1){
        throw std::logic_error("IR logical left operand requires an i1 value");
    }

    auto &builder = context_.builder;
    const bool isAnd = expression.op() == "&&";
    const IRValue address = builder.createEntryAlloca(IRType::I1);
    // 每次求值先保存短路结果；只有右侧正常结束时才覆盖它。
    builder.createStore(IRValue{IRType::I1, "", isAnd ? 0u : 1u}, address);

    const std::string suffix = std::to_string(basicBlockId++);
    auto &rightBlock = builder.createBlock("logical.rhs." + suffix);
    auto &mergeBlock = builder.createBlock("logical.end." + suffix);
    if(isAnd) builder.createIf(*left.value, rightBlock, mergeBlock);
    else builder.createIf(*left.value, mergeBlock, rightBlock);

    builder.setInsertPoint(rightBlock);
    const auto right = generateExpr(expression.right());
    if(!right.terminated){
        if(!right.value || right.value->type != IRType::I1){
            throw std::logic_error("IR logical right operand requires an i1 value");
        }
        // 嵌套表达式可能改变插入位置，从右侧实际结束的块连接合流块。
        builder.createStore(*right.value, address);
        builder.createBranch(mergeBlock);
    }

    builder.setInsertPoint(mergeBlock);
    return {builder.createLoad(IRType::I1, address), false};
}

IRExprResult IRGenerator::generateIf(const rx::ast::IfExpr &expression){
    auto condition = generateExpr(expression.condition());
    if(condition.terminated) return condition;

    if(!condition.value || condition.value->type != IRType::I1){
        throw std::logic_error("IR if condition requires an i1 value");
    }

    auto &builder = context_.builder;
    const auto &model = *context_.model;
    const TypeId type = model.expressionTypes.at(expression.id());
    const IRType resultType = model.types.get(type).kind() == TypeKind::Never ? IRType::Void : context_.typeLowering.lowerType(type, model);
    std::optional<IRValue> address;
    if(resultType != IRType::Void) address = builder.createEntryAlloca(resultType);

    const std::string suffix = std::to_string(basicBlockId++);
    auto &ifBlock = builder.createBlock("if.then." + suffix);
    auto &elseBlock = builder.createBlock((expression.elseBranch() ? "if.else." : "if.end.") + suffix);
    IRBasicBlock *mergeBlock = expression.elseBranch() ? nullptr : &elseBlock;
    builder.createIf(*condition.value, ifBlock, elseBlock);

    // 在各分支实际结束的位置连接合流块，嵌套 if 可能改变插入位置。
    const auto finishBranch = [&](const IRExprResult &result){
        if(result.terminated) return;  //检查该分支是否return，如果没有，做收尾工作
        if(address){
            if(!result.value || result.value->type != resultType){
                throw std::logic_error("IR if branch result does not match its type");
            }
            builder.createStore(*result.value, *address);
        }
        if(!mergeBlock) mergeBlock = &builder.createBlock("if.end." + suffix);
        builder.createBranch(*mergeBlock);
    };

    builder.setInsertPoint(ifBlock);
    finishBranch(generateCoercedExpr(expression.thenBranch()));
    if(expression.elseBranch()){
        builder.setInsertPoint(elseBlock);
        finishBranch(generateCoercedExpr(*expression.elseBranch()));
    }

    // 两个分支都已终结时不创建合流块，也不再生成 load。
    if(!mergeBlock) return {{}, true};
    builder.setInsertPoint(*mergeBlock);
    if(address) return {builder.createLoad(resultType, *address), false};
    return {};
}

IRExprResult IRGenerator::generateContinue(const rx::ast::ContinueExpr &expression){
    if(context_.loops.empty()){
        throw std::logic_error("IR continue requires an enclosing loop");
    }

    context_.builder.createBranch(*context_.loops.top().continueBlock);
    return {{}, true};
}

IRExprResult IRGenerator::generateBreak(const rx::ast::BreakExpr &expression){
    if(context_.loops.empty()){
        throw std::logic_error("IR break requires an enclosing loop");
    }

    const auto result = expression.value() ? generateCoercedExpr(*expression.value()) : IRExprResult{};
    if(result.terminated) return result;

    auto &builder = context_.builder;
    auto &loop = context_.loops.top();
    if(loop.resultAddress){
        if(!result.value || result.value->type != loop.resultType){
            throw std::logic_error("IR break value does not match the loop type");
        }
        builder.createStore(*result.value, *loop.resultAddress);
    }else if(result.value){
        throw std::logic_error("IR unit loop cannot receive a break value");
    }

    if(!loop.breakblock) loop.breakblock = &builder.createBlock(loop.exitName);
    builder.createBranch(*loop.breakblock);
    return {{}, true};
}

IRExprResult IRGenerator::generateLoop(const rx::ast::LoopExpr &expression){
    auto &builder = context_.builder;
    const auto &model = *context_.model;
    const TypeId type = model.expressionTypes.at(expression.id());
    const IRType resultType = model.types.get(type).kind() == TypeKind::Never ? IRType::Void : context_.typeLowering.lowerType(type, model);
    std::optional<IRValue> address;
    if(resultType != IRType::Void) address = builder.createEntryAlloca(resultType);

    const std::string suffix = std::to_string(basicBlockId++);
    auto &loopBlock = builder.createBlock("loop.body." + suffix);
    IRBasicBlock *exitBlock = nullptr;
    builder.createBranch(loopBlock);

    IRExprResult result;
    {
        context_.loops.push({&loopBlock, nullptr, resultType, address, "loop.exit." + suffix});
        struct LoopGuard{
            std::stack<IRLoop> &loops;
            ~LoopGuard(){ loops.pop(); }
        } guard{context_.loops};

        builder.setInsertPoint(loopBlock);
        result = generateBlock(expression.body());
        exitBlock = context_.loops.top().breakblock;
    }

    if(!result.terminated){
        builder.createBranch(loopBlock);
    }

    // 只有实际生成的 break 才能建立正常出口，不能仅凭循环体的 terminated 判断。
    if(!exitBlock) return {{}, true};
    builder.setInsertPoint(*exitBlock);
    if(address) return {builder.createLoad(resultType, *address), false};
    return {};
}

IRExprResult IRGenerator::generateWhile(const rx::ast::WhileExpr &expression){
    auto &builder = context_.builder;
    
    const std::string suffix = std::to_string(basicBlockId++);
    auto &conditionBlock = builder.createBlock("while.cond." + suffix);
    builder.createBranch(conditionBlock);

    builder.setInsertPoint(conditionBlock);
    const auto condition = generateExpr(expression.condition());
    if(condition.terminated) return condition;

    if(!condition.value || condition.value->type != IRType::I1){
        throw std::logic_error("IR while condition requires an i1 value");
    }

    auto &bodyBlock = builder.createBlock("while.body." + suffix);
    auto &exitBlock = builder.createBlock("while.end." + suffix);
    builder.createIf(*condition.value, bodyBlock, exitBlock);

    IRExprResult result;
    {
        context_.loops.push({&conditionBlock, &exitBlock});
        struct LoopGuard{
            std::stack<IRLoop> &loops;
            ~LoopGuard(){ loops.pop(); }
        } guard{context_.loops};

        builder.setInsertPoint(bodyBlock);
        result = generateBlock(expression.body());
    }
    
    if(!result.terminated){
        builder.createBranch(conditionBlock);
    }

    builder.setInsertPoint(exitBlock);
    return{};
}
