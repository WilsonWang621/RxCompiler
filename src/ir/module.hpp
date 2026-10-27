#pragma once

#include "ir.hpp"

#include <ostream>

class IRModule{ //拥有全部 IR 数据
private:
    friend class IRBuilder;
    std::vector<std::unique_ptr<IRFunction>> functions_;
    std::vector<std::unique_ptr<IRStruct>> structs_;

public:
    const std::vector<std::unique_ptr<IRFunction>> &functions() const;
    const std::vector<std::unique_ptr<IRStruct>> &structs() const;
    void dump(std::ostream &out, int indent = 0) const;
};
