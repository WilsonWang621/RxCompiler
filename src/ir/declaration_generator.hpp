#pragma once

#include "generation_context.hpp"

class DeclarationGenerator{ //第一遍扫描：登记类型、常量和完整函数签名。
public:
    // 遍历所有 Item；ImplItem 递归访问 items()，此阶段不生成函数体。
    void declareItem(const rx::ast::Item &item, IRGenerationContext &context);

private:
    void declareStruct(const rx::ast::StructItem &item, IRGenerationContext &context);
    void declareConstant(const rx::ast::ConstItem &item, IRGenerationContext &context);
    void declareFunction(const rx::ast::FunctionItem &function, IRGenerationContext &context);
    // 使用 SymbolId 保证普通函数、关联函数、常量的名字不会冲突。
    std::string mangleSymbol(SymbolId symbol, const SemanticModel &model) const;
};
