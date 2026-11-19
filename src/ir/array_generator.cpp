#include "ir_generator.hpp"

#include <stdexcept>
#include <cstdint>
#include <string>


IRValue IRGenerator::materializeArrayElement(const IRExprResult &result, TypeId sourceType, TypeId targetType){
    const auto &model = *context_.model;
    auto value = materializeValue(result, sourceType);
    if(sourceType == targetType) return value;

    const auto &source = model.types.get(sourceType);
    const auto &target = model.types.get(targetType);
    if(source.kind() != TypeKind::Reference || target.kind() != TypeKind::Reference){
        throw std::logic_error("IR array element has an unsupported coercion");
    }

    // 指针当前指向 source 的目标；继续解引用直到目标类型匹配。
    TypeId current = source.elementType();
    while(current != target.elementType()){
        const auto &info = model.types.get(current);
        if(info.kind() != TypeKind::Reference && info.kind() != TypeKind::Box){
            throw std::logic_error("IR array element coercion cannot reach its target");
        }
        value = context_.builder.createLoad(IRType::Ptr, value);
        current = info.elementType();
    }
    return value;
}

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
        const auto result = generateExpr(*element);
        if(result.terminated) return result;

        const auto value = materializeArrayElement(result, model.expressionTypes.at(element->id()), info.elementType());
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
    const auto result = generateExpr(expression.value());
    if(result.terminated) return result;
    //分配数组和循环计数器
    const auto value = materializeArrayElement(result, model.expressionTypes.at(expression.value().id()), info.elementType());
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
    IRPlace place;

    if(model.expressionInfo.at(base.id()).isPlace){
        const auto result = generatePlace(base);
        if(!result) return std::nullopt;
        place = *result;
    }else{
        // 临时数组只求值一次，保存后再访问其中的元素。
        const auto result = generateExpr(base);
        if(result.terminated) return std::nullopt;
        const TypeId type = model.expressionTypes.at(base.id());
        const auto value = materializeValue(result, type);
        const auto address = builder.createEntryAlloca(value.type);
        builder.createStore(value, address);
        place = IRPlace{address, type};
    }

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

    const auto result = generateExpr(expression.index());
    if(result.terminated) return std::nullopt;
    const auto index = materializeValue(result, model.expressionTypes.at(expression.index().id()));
    const IRType arrayType = context_.typeLowering.lowerStorageType(place.type, model);
    const auto address = builder.createArrayElementAddress(arrayType, place.address, index);
    return IRPlace{address, info.elementType()};
}
