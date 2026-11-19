#include "ir_generator.hpp"

#include <stdexcept>
#include <cstdint>
#include <string>


IRExprResult IRGenerator::generateArray(const rx::ast::ArrayExpr &expression){
    const auto &model = *context_.model;
    auto &builder = context_.builder;
    const TypeId type = model.expressionTypes.at(expression.id());
    const auto &info = model.types.get(type);
    if(info.kind() != TypeKind::Array || info.length() != expression.elements().size()){
        throw std::logic_error("IR array literal type doesn't match its elements");
    }
    const IRType arrayType = context_.typeLowering.lowerStorageType(type, model);
    const auto address = builder.createEntryAlloca(arrayType);
    uint32_t index = 0;
    for(const auto &element : expression.elements()){
        const auto result = generateCoercedExpr(*element);
        if(result.terminated) return result;

        const auto value = materializeValue(result, info.elementType());
        const auto elementAddress = builder.createArrayElementAddress(arrayType, address, IRValue{IRType::I32, "", index++});
        builder.createStore(value, elementAddress);
    }
    return {builder.createLoad(arrayType, address), false};
}

IRExprResult IRGenerator::generateRepeatedArray(const rx::ast::ArrayRepeatExpr &expression){
    //从语义模型取得类型和长度
    const auto &model = *context_.model;
    auto &builder = context_.builder;
    const TypeId type = model.expressionTypes.at(expression.id());
    const auto &info = model.types.get(type);
    if(info.kind() != TypeKind::Array) throw std::logic_error("IR repetition requires an array type");
    //生成一次元素表达式
    const auto result = generateCoercedExpr(expression.value());
    if(result.terminated) return result;
    //分配数组和循环计数器
    const auto value = materializeValue(result, info.elementType());
    const IRType arrayType = context_.typeLowering.lowerStorageType(type, model);
    const auto address = builder.createEntryAlloca(arrayType);
    const auto indexAddress = builder.createEntryAlloca(IRType::I32);
    builder.createStore(IRValue{IRType::I32, "", 0}, indexAddress);
    //生三个基本块
    const std::string suffix = std::to_string(basicBlockId++);
    auto &conditionBlock = builder.createBlock("array.repeat.cond." + suffix); //读取下标，判断index < N
    auto &bodyBlock = builder.createBlock("array.repeat.body." + suffix); //写当前元素，下标+1 跳回condition
    auto &exitBlock = builder.createBlock("array.repeat.end." + suffix); //初始化结束，取得结果
    builder.createBranch(conditionBlock);

    builder.setInsertPoint(conditionBlock);
    const auto index = builder.createLoad(IRType::I32, indexAddress);
    const auto condition = builder.createCompare(IRComparePredicate::Ult, index, IRValue{IRType::I32, "", info.length()});
    builder.createIf(condition, bodyBlock, exitBlock);

    builder.setInsertPoint(bodyBlock);
    const auto elementAddress = builder.createArrayElementAddress(arrayType, address, index);
    builder.createStore(value, elementAddress);
    const auto next = builder.createBinary(IROpcode::Add, index, IRValue{IRType::I32, "", 1});
    builder.createStore(next, indexAddress);
    builder.createBranch(conditionBlock);

    builder.setInsertPoint(exitBlock);
    return {builder.createLoad(arrayType, address), false};
}

std::optional<IRPlace> IRGenerator::generateIndexPlace(const rx::ast::IndexExpr &expression){
    const auto &model = *context_.model;
    auto &builder = context_.builder;
    const auto &base = expression.base();
    const auto result = generatePlaceOrTemporary(base);
    if(!result) return std::nullopt;
    IRPlace place = *result;

    while(true){
        const auto &info = model.types.get(place.type);
        if(info.kind() != TypeKind::Reference && info.kind() != TypeKind::Box) break;
        place.address = builder.createLoad(IRType::Ptr, place.address);
        place.type = info.elementType();
    }

    const auto &info = model.types.get(place.type);
    if(info.kind() != TypeKind::Array){
        throw std::runtime_error("IR indexing currently requires a fixed array");
    }

    const auto indexResult = generateExpr(expression.index());
    if(indexResult.terminated) return std::nullopt;
    const auto index = materializeValue(indexResult, model.expressionTypes.at(expression.index().id()));
    const IRType arrayType = context_.typeLowering.lowerStorageType(place.type, model);
    const auto address = builder.createArrayElementAddress(arrayType, place.address, index);
    return IRPlace{address, info.elementType()};
}
