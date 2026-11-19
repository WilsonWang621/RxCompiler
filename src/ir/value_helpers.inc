IRFunction &IRGenerator::valueHelper(TypeId type, bool clone){
    auto &functions = clone ? context_.cloneFunctions : context_.equalityFunctions;
    const auto found = functions.find(type);
    if(found != functions.end()) return *found->second;
    std::vector<IRParameter> parameters{{IRType::Ptr, "left"}};
    if(!clone) parameters.push_back({IRType::Ptr, "right"});
    const IRType result = clone ? context_.typeLowering.lowerStorageType(type, *context_.model) : IRType{IRType::I1};
    auto &function = context_.builder.declareFunction(
        std::string(clone ? "rx.clone." : "rx.eq.") + std::to_string(type.value), result, std::move(parameters));
    functions.emplace(type, &function);
    context_.valueHelpers.push_back({type, &function, clone});
    return function;
}

void IRGenerator::generateValueHelpers(){
    auto &builder = context_.builder;
    // 生成函数体时可能登记新的帮助函数，递归调用可直接引用已登记的签名。
    for(std::size_t index = 0; index < context_.valueHelpers.size(); ++index){
        const auto helper = context_.valueHelpers[index];
        builder.beginFunction(*helper.function);
        builder.setInsertPoint(builder.createBlock("entry"));
        const IRValue left{IRType::Ptr, "left", 0};
        if(helper.clone) generateCloneBody(helper.type, left);
        else generateEqualityBody(helper.type, left, {IRType::Ptr, "right", 0});
    }
}

void IRGenerator::generateCloneBody(TypeId type, IRValue source){
    auto &builder = context_.builder;
    const auto &model = *context_.model;
    const auto &info = model.types.get(type);
    const auto storage = context_.typeLowering.lowerStorageType(type, model);
    const auto cloneAt = [&](TypeId member, IRValue address){
        return builder.createCall(valueHelper(member, true), {address}).value();
    };
    if(info.kind() == TypeKind::Box){
        const auto element = context_.typeLowering.lowerStorageType(info.elementType(), model);
        const auto input = builder.createLoad(IRType::Ptr, source);
        const auto output = allocateElements(element, word(1));
        builder.createStore(cloneAt(info.elementType(), input), output);
        builder.createReturn(output);
        return;
    }
    if(info.kind() == TypeKind::Vec){
        const auto output = emptyVector();
        const auto length = builder.createLoad(IRType::I32, vectorField(source, VectorLength));
        auto &copy = builder.createBlock("vec.clone.copy");
        auto &done = builder.createBlock("vec.clone.done");
        builder.createIf(builder.createCompare(IRComparePredicate::Eq, length, word(0)), done, copy);
        builder.setInsertPoint(copy);
        const auto element = context_.typeLowering.lowerStorageType(info.elementType(), model);
        const auto input = builder.createLoad(IRType::Ptr, vectorField(source, VectorData));
        const auto data = allocateElements(element, length);
        forEachElement(length, [&](IRValue index){
            const auto value = cloneAt(info.elementType(), elementAddress(element, input, index));
            builder.createStore(value, elementAddress(element, data, index));
        });
        builder.createStore(data, vectorField(output, VectorData));
        builder.createStore(length, vectorField(output, VectorLength));
        builder.createStore(length, vectorField(output, VectorCapacity));
        builder.createBranch(done);
        builder.setInsertPoint(done);
        builder.createReturn(builder.createLoad(storage, output));
        return;
    }
    if(info.kind() == TypeKind::Array || info.kind() == TypeKind::Struct){
        const auto output = builder.createEntryAlloca(storage);
        if(info.kind() == TypeKind::Array){
            forEachElement(word(info.length()), [&](IRValue index){
                const auto value = cloneAt(info.elementType(), builder.createArrayElementAddress(storage, source, index));
                builder.createStore(value, builder.createArrayElementAddress(storage, output, index));
            });
        }else{
            const auto &declaration = model.declarations.structs.at(info.symbol());
            std::size_t index = 0;
            for(const auto &field : declaration.declaration->fields()){
                const auto input = builder.createFieldAddress(*storage.structure(), source, index);
                const auto value = cloneAt(declaration.fieldTypes.at(field->name()), input);
                builder.createStore(value, builder.createFieldAddress(*storage.structure(), output, index));
                ++index;
            }
        }
        builder.createReturn(builder.createLoad(storage, output));
        return;
    }
    // 标量直接复制，共享引用复制其地址。
    builder.createReturn(builder.createLoad(storage, source));
}

void IRGenerator::generateEqualityBody(TypeId type, IRValue left, IRValue right){
    auto &builder = context_.builder;
    const auto &model = *context_.model;
    const auto &info = model.types.get(type);
    const auto storage = context_.typeLowering.lowerStorageType(type, model);
    const auto equalAt = [&](TypeId member, IRValue a, IRValue b){
        return builder.createCall(valueHelper(member, false), {a, b}).value();
    };
    if(info.kind() == TypeKind::Reference || info.kind() == TypeKind::Box){
        const auto a = builder.createLoad(IRType::Ptr, left);
        const auto b = builder.createLoad(IRType::Ptr, right);
        builder.createReturn(equalAt(info.elementType(), a, b));
        return;
    }
    if(info.kind() == TypeKind::Unit){
        builder.createReturn(boolean(true));
        return;
    }
    if(info.isInteger() || info.kind() == TypeKind::Bool){
        const auto a = builder.createLoad(storage, left);
        const auto b = builder.createLoad(storage, right);
        builder.createReturn(builder.createCompare(IRComparePredicate::Eq, a, b));
        return;
    }
    auto &different = builder.createBlock("eq.different");
    const auto requireEqual = [&](IRValue condition){
        auto &next = builder.createBlock("eq.next." + std::to_string(basicBlockId++));
        builder.createIf(condition, next, different);
        builder.setInsertPoint(next);
    };
    if(info.kind() == TypeKind::Struct){
        const auto &declaration = model.declarations.structs.at(info.symbol());
        std::size_t index = 0;
        for(const auto &field : declaration.declaration->fields()){
            const auto a = builder.createFieldAddress(*storage.structure(), left, index);
            const auto b = builder.createFieldAddress(*storage.structure(), right, index);
            requireEqual(equalAt(declaration.fieldTypes.at(field->name()), a, b));
            ++index;
        }
    }else if(info.kind() == TypeKind::Array){
        forEachElement(word(info.length()), [&](IRValue index){
            const auto a = builder.createArrayElementAddress(storage, left, index);
            const auto b = builder.createArrayElementAddress(storage, right, index);
            requireEqual(equalAt(info.elementType(), a, b));
        });
    }else if(info.kind() == TypeKind::Vec){
        const auto aLength = builder.createLoad(IRType::I32, vectorField(left, VectorLength));
        const auto bLength = builder.createLoad(IRType::I32, vectorField(right, VectorLength));
        requireEqual(builder.createCompare(IRComparePredicate::Eq, aLength, bLength));
        const auto aData = builder.createLoad(IRType::Ptr, vectorField(left, VectorData));
        const auto bData = builder.createLoad(IRType::Ptr, vectorField(right, VectorData));
        const auto element = context_.typeLowering.lowerStorageType(info.elementType(), model);
        forEachElement(aLength, [&](IRValue index){
            const auto a = elementAddress(element, aData, index);
            const auto b = elementAddress(element, bData, index);
            requireEqual(equalAt(info.elementType(), a, b));
        });
    }else{
        throw std::logic_error("unknown IR equality type");
    }
    builder.createReturn(boolean(true));
    builder.setInsertPoint(different);
    builder.createReturn(boolean(false));
}

IRExprResult IRGenerator::generateEquality(const rx::ast::BinaryExpr &expression){
    const auto left = generatePlaceOrTemporary(expression.left());
    if(!left) return {{}, true};
    const auto right = generatePlaceOrTemporary(expression.right());
    if(!right) return {{}, true};
    auto &builder = context_.builder;
    auto result = builder.createCall(valueHelper(left->type, false), {left->address, right->address}).value();
    if(expression.op() == "!=") result = builder.createBinary(IROpcode::Xor, result, boolean(true));
    return {result, false};
}
