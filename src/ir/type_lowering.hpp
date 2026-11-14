#pragma once

#include "ir.hpp"
#include "semantic_model.hpp"

#include <unordered_map>

class TypeLowering{ //把语义分析阶段的类型，转换成 IR 阶段使用的类型
private:
    std::unordered_map<SymbolId, const IRStruct *> structs_;
public:
    // 函数返回和控制流结果使用此接口：unit 不产生运行时值。
    IRType lowerType(TypeId type, const SemanticModel &model) const {
        if(model.types.get(type).kind() == TypeKind::Unit) return IRType::Void;
        return lowerStorageType(type, model);
    }

    IRType lowerStorageType(TypeId type, const SemanticModel &model) const{ // 返回这个类型在内存中对应的IRType
        const auto &info = model.types.get(type);
        switch(info.kind()){
            case TypeKind::Unit:
                return IRType::Unit;
            case TypeKind::Bool:
                return IRType::I1;
            case TypeKind::I32: case TypeKind::U32:
            case TypeKind::Isize: case TypeKind::Usize:
                return IRType::I32;
            case TypeKind::Reference:
                return IRType::Ptr;
            case TypeKind::Struct:{
                const auto found = structs_.find(info.symbol());
                if(found == structs_.end()) throw std::logic_error("IR struct type is not registered");
                return IRType::structure(*found->second);
            }
            case TypeKind::Array:
                return IRType::array(lowerStorageType(info.elementType(), model), info.length());
            default:
                throw std::runtime_error("semantic type is not supported by IR lowering yet");
        }
    }

    void registerStruct(SymbolId symbol, const IRStruct &structure){
        if(!structs_.emplace(symbol, &structure).second){
            throw std::logic_error("IR struct type is already registered");
        }
    }

    void clear(){
        structs_.clear();
    }
};
