#include "function_generator.hpp"

#include <stdexcept>

void FunctionGenerator::generateFunction(const rx::ast::FunctionItem &function, IRGenerationContext &){
    // TODO: 使用已登记的函数绑定参数，再交给 StatementGenerator 生成函数体。
    throw std::runtime_error("IR function body is not implemented: " + function.name());
}

void FunctionGenerator::generateMainWrapper(IRGenerationContext &){
    // TODO: 调用已登记的源码 main，并让机器入口返回 i32 的 0。
    throw std::runtime_error("IR main wrapper is not implemented");
}
