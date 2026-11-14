#pragma once

#include <cstdint>
#include <memory>
#include <stdexcept>

struct IRStruct;

class IRType{
public:
    enum Kind{Void, I32, Ptr, I1, Unit, Struct, Array};

private:
    Kind kind_ = I32;
    const IRStruct *structure_ = nullptr; //指向module的结构体，但是不负责释放
    std::shared_ptr<const IRType> elementType_; //元素类型描述 便于复制嵌套
    std::uint32_t length_ = 0; //经过语义分析和常量求值的数组长度

public:
    IRType(Kind kind = I32){
        if(kind == Kind::Array || kind == Kind::Struct) throw std::invalid_argument("aggregate types require a factory");
        kind_ = kind;
    }

    static IRType structure(const IRStruct &structure){
        IRType result;
        result.kind_ = Kind::Struct;
        result.structure_ = &structure;
        return result;
    }

    static IRType array(IRType elementType, std::uint32_t length){
        if(elementType == Kind::Void) throw std::invalid_argument("array element type cannot be void");
        IRType result;
        result.kind_ = Kind::Array;
        result.length_ = length;
        result.elementType_ = std::make_shared<const IRType>(elementType);
        return result;
    }

    Kind kind() const { return kind_; }

    const IRStruct *structure() const { return structure_; }

    const IRType &elementType() const{
        if (kind_ != Kind::Array || !elementType_){
            throw std::logic_error("element type requires an array");
        }
        return *elementType_;
    }

    std::uint32_t length() const { return length_; }

    bool operator==(const IRType &other) const{
        if(kind_ != other.kind_) return false;
        if(kind_ == Kind::Struct){
            return structure_ == other.structure_;
        }
        if(kind_ == Kind::Array){
            return length_ == other.length_ && *elementType_ == *other.elementType_;
        }
        return true;
    }

    bool operator!=(const IRType &other) const {
        return !(*this == other);
    }
};
