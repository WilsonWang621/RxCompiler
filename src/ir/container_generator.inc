#include "data_layout.hpp"

namespace{
IRValue word(std::uint32_t bits){ return {IRType::I32, "", bits}; }
IRValue boolean(bool value){ return {IRType::I1, "", value ? 1u : 0u}; }
enum VectorField{VectorData, VectorLength, VectorCapacity};
}

IRValue IRGenerator::allocateElements(IRType element, IRValue count){
    auto &builder = context_.builder;
    if(!context_.mallocFunction){
        context_.mallocFunction = &builder.declareFunction("malloc", IRType::Ptr, {{IRType::I32, "size"}}, false);
    }
    const auto stride = storageLayout(element).size;
    if(!stride || stride > UINT32_MAX) throw std::runtime_error("invalid IR heap element size");
    const auto bytes = builder.createBinary(IROpcode::Mul, count, word(static_cast<std::uint32_t>(stride)));
    return builder.createCall(*context_.mallocFunction, {bytes}).value();
}

IRValue IRGenerator::elementAddress(IRType element, IRValue base, IRValue index){
    // 非 inbounds GEP 只按元素布局计算步长，缓冲区长度由 Vec 持有。
    return context_.builder.createArrayElementAddress(IRType::array(element, 0), base, index);
}

IRValue IRGenerator::vectorField(IRValue address, std::size_t index){
    return context_.builder.createFieldAddress(*context_.vectorStorage, address, index);
}

IRValue IRGenerator::vectorElementAddress(IRType element, IRValue vector, IRValue index){
    const auto data = context_.builder.createLoad(IRType::Ptr, vectorField(vector, VectorData));
    return elementAddress(element, data, index);
}

IRValue IRGenerator::emptyVector(){
    auto &builder = context_.builder;
    const auto storage = IRType::structure(*context_.vectorStorage);
    const auto address = builder.createEntryAlloca(storage);
    builder.createStore({IRType::Ptr, "", 0}, vectorField(address, VectorData));
    builder.createStore(word(0), vectorField(address, VectorLength));
    builder.createStore(word(0), vectorField(address, VectorCapacity));
    return address;
}

void IRGenerator::forEachElement(IRValue count, const std::function<void(IRValue)> &body){
    auto &builder = context_.builder;
    const auto counter = builder.createEntryAlloca(IRType::I32);
    builder.createStore(word(0), counter);
    const auto suffix = std::to_string(basicBlockId++);
    auto &test = builder.createBlock("elements.test." + suffix);
    auto &step = builder.createBlock("elements.step." + suffix);
    auto &done = builder.createBlock("elements.done." + suffix);
    builder.createBranch(test);
    builder.setInsertPoint(test);
    const auto index = builder.createLoad(IRType::I32, counter);
    builder.createIf(builder.createCompare(IRComparePredicate::Ult, index, count), step, done);
    builder.setInsertPoint(step);
    body(index);
    builder.createStore(builder.createBinary(IROpcode::Add, index, word(1)), counter);
    builder.createBranch(test);
    builder.setInsertPoint(done);
}

IRExprResult IRGenerator::emitContainerCall(const ResolvedCall &call, const std::vector<IRValue> &arguments){
    auto &builder = context_.builder;
    const auto &model = *context_.model;
    const auto operation = call.target.builtin;
    const auto &owner = model.types.get(call.target.ownerType);
    const auto element = context_.typeLowering.lowerStorageType(owner.elementType(), model);
    if(operation == BuiltinOperation::BoxNew){
        const auto address = allocateElements(element, word(1));
        builder.createStore(arguments.at(0), address);
        return {address, false};
    }
    if(operation == BuiltinOperation::VecNew){
        return {builder.createLoad(IRType::structure(*context_.vectorStorage), emptyVector()), false};
    }
    const auto vector = arguments.at(0);
    const auto lengthAddress = vectorField(vector, VectorLength);
    const auto length = builder.createLoad(IRType::I32, lengthAddress);
    if(operation == BuiltinOperation::VecLen) return {length, false};
    if(operation == BuiltinOperation::VecIsEmpty){
        return {builder.createCompare(IRComparePredicate::Eq, length, word(0)), false};
    }
    const auto dataAddress = vectorField(vector, VectorData);
    const auto one = word(1);
    if(operation == BuiltinOperation::VecRemove){
        const auto data = builder.createLoad(IRType::Ptr, dataAddress);
        const auto index = arguments.at(1);
        const auto result = builder.createLoad(element, elementAddress(element, data, index));
        const auto newLength = builder.createBinary(IROpcode::Sub, length, one);
        const auto tail = builder.createBinary(IROpcode::Sub, newLength, index);
        forEachElement(tail, [&](IRValue offset){
            const auto to = builder.createBinary(IROpcode::Add, index, offset);
            const auto from = builder.createBinary(IROpcode::Add, to, one);
            const auto value = builder.createLoad(element, elementAddress(element, data, from));
            builder.createStore(value, elementAddress(element, data, to));
        });
        builder.createStore(newLength, lengthAddress);
        return {result, false};
    }
    if(operation != BuiltinOperation::VecPush) throw std::logic_error("unknown IR container operation");

    const auto capacityAddress = vectorField(vector, VectorCapacity);
    const auto capacity = builder.createLoad(IRType::I32, capacityAddress);
    const auto suffix = std::to_string(basicBlockId++);
    auto &grow = builder.createBlock("vec.grow." + suffix);
    auto &append = builder.createBlock("vec.append." + suffix);
    builder.createIf(builder.createCompare(IRComparePredicate::Eq, length, capacity), grow, append);
    builder.setInsertPoint(grow);
    // 空 Vec 的初始容量为 4，后续每次扩容翻倍。
    const auto fresh = builder.createCompare(IRComparePredicate::Eq, capacity, word(0));
    const auto initial = builder.createBinary(IROpcode::Mul, builder.createZExt(fresh), word(4));
    const auto doubled = builder.createBinary(IROpcode::Add, capacity, capacity);
    const auto newCapacity = builder.createBinary(IROpcode::Add, doubled, initial);
    const auto data = allocateElements(element, newCapacity);
    const auto oldData = builder.createLoad(IRType::Ptr, dataAddress);
    forEachElement(length, [&](IRValue index){
        const auto value = builder.createLoad(element, elementAddress(element, oldData, index));
        builder.createStore(value, elementAddress(element, data, index));
    });
    builder.createStore(data, dataAddress);
    builder.createStore(newCapacity, capacityAddress);
    builder.createBranch(append);
    builder.setInsertPoint(append);
    const auto buffer = builder.createLoad(IRType::Ptr, dataAddress);
    builder.createStore(arguments.at(1), elementAddress(element, buffer, length));
    builder.createStore(builder.createBinary(IROpcode::Add, length, one), lengthAddress);
    return {};
}
