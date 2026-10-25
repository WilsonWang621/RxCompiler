#pragma once

#include "semantic_model.hpp"
#include "scope.hpp"
#include "ast.hpp"
#include "declaration_collector.hpp"
#include "function_collector.hpp"
#include "resolution.hpp"
#include "const_evaluate.hpp"
#include "statement_collector.hpp"
#include "expression_collector.hpp"
#include "control_flow_collector.hpp"
#include "types_checker.hpp"

#include <cstdint>
#include <optional>
#include <string>
#include <unordered_map>

class SemanticChecker{
private:
    friend class DeclarationCollector;
    friend class FunctionCollector;
    friend class Resolution;
    friend class ConstEvaluate;
    friend class StatementCollector;
    friend class ExpressionCollector;
    friend class ControlFlowCollector;
    friend class TypesChecker;

    SemanticModel model_;
    Scope scope_;
    std::unordered_map<std::string, SymbolId> builtinSymbols_;
    std::optional<TypeId> currentReturnType_;     //当前函数声明的返回类型，collectReturn 用它检查 return 的值是否兼容
    std::optional<TypeId> currentSelfType_;      //当前环境中 Self 代表的具体类型。类型解析和路径解析用它处理
    enum class ConstantState{ Evaluating, Complete, Failed };
    // 表中不存在的符号尚未开始求值。
    std::unordered_map<SymbolId, ConstantState> constantStates_;
    std::uint32_t nextSymbolId_ = 1;

    // 各模块共享当前 checker 的模型和上下文，不单独保存语义状态。
    DeclarationCollector declarationCollector_{*this};
    FunctionCollector functionCollector_{*this};
    Resolution resolution_{*this};
    ConstEvaluate constEvaluate_{*this};
    StatementCollector statementCollector_{*this};
    ExpressionCollector expressionCollector_{*this};
    ControlFlowCollector controlFlowCollector_{*this};
    TypesChecker typesChecker_{*this};

    // 检查流程、符号分配和诊断。
    SymbolId allocateSymbol();
    void diagnose(const rx::ast::ASTNode &node, std::string message);
    void firstScan(const rx::ast::Crate &crate);    //第一次扫描收集信息和做部分检查
    void secondScan(const rx::ast::Crate &crate);   //第二次扫描完成剩余检查
    void checkMain(const rx::ast::Crate &crate);

public:
    SemanticChecker() = default;
    // 模块持有当前对象的引用，禁止复制和移动以保持引用有效。
    SemanticChecker(const SemanticChecker &) = delete;
    SemanticChecker &operator=(const SemanticChecker &) = delete;
    SemanticChecker(SemanticChecker &&) = delete;
    SemanticChecker &operator=(SemanticChecker &&) = delete;

    SemanticModel check(const rx::ast::Crate &crate);
};
