#include "ir_generator.hpp"

// TODO: 补充分支和循环。
IRExprResult IRGenerator::generateReturn(const rx::ast::ReturnExpr &expression){
    const auto result = expression.value() ? generateExpr(*expression.value()) : IRExprResult{};
    if(!result.terminated) context_.builder.createReturn(result.value);
    return {{}, true};
}
