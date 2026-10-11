#pragma once

#include <cstddef>

namespace rx::ast{

struct SourcePosition{
    // 与 ANTLR 输入流一致：offset 从 0 开始，按 Unicode 码点计数，不是 UTF-8 字节。
    std::size_t offset = 0;
    // 行列从 1 开始；0 表示尚未关联源码。制表符计为一个字符。
    std::size_t line = 0;
    std::size_t column = 0;

    bool valid() const { return line != 0 && column != 0; }
};

// 当前编译器一次处理一个文件，位置均相对于该输入文件。
// 使用左闭右开区间 [begin, end)，空区间可以表示 EOF 等插入位置。
struct SourceSpan{
    SourcePosition begin;
    SourcePosition end;

    bool valid() const {
        return begin.valid() && end.valid() && begin.offset <= end.offset;
    }

    // 合并同一文件中的范围；未知范围不参与合并。
    static SourceSpan covering(const SourceSpan &left, const SourceSpan &right){
        if(!left.valid()) return right;
        if(!right.valid()) return left;
        return {
            left.begin.offset <= right.begin.offset ? left.begin : right.begin,
            left.end.offset >= right.end.offset ? left.end : right.end
        };
    }
};

}
