#pragma once

#include "declaration_generator.hpp"
#include "function_generator.hpp"

class IRGenerator{ //只负责本次生成的初始化、两遍 Item 扫描和模块交付。
private:
    IRGenerationContext context_;
    DeclarationGenerator declarationGenerator_;
    FunctionGenerator functionGenerator_;
    void generateItem(const rx::ast::Item &item, IRGenerationContext &context);

public:
    IRGenerator() = default;
    IRGenerator(const IRGenerator &) = delete;
    IRGenerator &operator=(const IRGenerator &) = delete;
    IRGenerator(IRGenerator &&) = delete;
    IRGenerator &operator=(IRGenerator &&) = delete;

    // reset -> 声明扫描 -> 函数体扫描 -> 包装器 -> clear。
    // 具体生成步骤尚未实现时抛出异常；成功或异常退出均清理上下文。
    IRModule generate(const rx::ast::Crate &crate, const SemanticModel &model);
};
