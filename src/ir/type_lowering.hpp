#pragma once

#include "ir.hpp"
#include "semantic_model.hpp"

class TypeLowering{ //把语义分析阶段的类型，转换成 IR 阶段使用的类型
public:
    // 当前覆盖 unit -> Void、bool -> I1、整数 -> I32、引用 -> Ptr。
    // 结构体的字段声明由 IRGenerator::declareStruct 登记到 IRStruct。
    IRType lowerType(TypeId type, const SemanticModel &model) const {
        switch(model.types.get(type).kind()){
            case TypeKind::Unit:
                return IRType::Void;
            case TypeKind::Bool:
                return IRType::I1;
            case TypeKind::I32: case TypeKind::U32:
            case TypeKind::Isize: case TypeKind::Usize:
                return IRType::I32;
            case TypeKind::Reference:
                return IRType::Ptr;
            default:
                throw std::runtime_error("semantic type is not supported by IR lowering yet");
        }
    }
};
