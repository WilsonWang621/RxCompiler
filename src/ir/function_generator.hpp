#pragma once

#include "statement_generator.hpp"

class FunctionGenerator{ //使用第一遍登记的函数，生成参数绑定、函数体和返回。
private:
    StatementGenerator statementGenerator_;
    // self 在首位；为入参建栈槽，mut 参数修改栈槽，不改写 SSA 入参。
    void bindParameters(const rx::ast::FunctionItem &function, IRGenerationContext &context);

public:
    // 进入函数清空 locals；块已终结则结束，否则返回尾值或 unit。
    void generateFunction(const rx::ast::FunctionItem &function, IRGenerationContext &context);
    void generateMainWrapper(IRGenerationContext &context); // 全部函数体生成后调用一次。
};
