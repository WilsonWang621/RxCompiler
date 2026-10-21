#include "control_flow_collector.hpp"
#include "semantic_checker.hpp"

TypeId ControlFlowCollector::collectIf(const rx::ast::IfExpr &ifstmt, std::optional<TypeId> expectedType){
    // 要求条件为 bool，检查各分支，并按预期类型或 LUB 确定整个 if 的类型。
}

TypeId ControlFlowCollector::collectLoop(const rx::ast::LoopExpr &loop, std::optional<TypeId> expectedType){
    // 建立循环上下文，收集本层 break 值并确定循环结果类型；无正常出口时可为 never。
}

TypeId ControlFlowCollector::collectWhile(const rx::ast::WhileExpr &loop){
    // 要求条件为 bool，在循环上下文中检查 unit 循环体以及 break 和 continue。
}

TypeId ControlFlowCollector::collectReturn(const rx::ast::ReturnExpr &returnstmt){
    // 以当前函数返回类型检查返回值或 bare return，并将表达式类型记为 never。
}

TypeId ControlFlowCollector::collectBreak(const rx::ast::BreakExpr &breakstmt){
    // 确认位于循环内，把值加入最近循环的结果候选，并将表达式类型记为 never。
}

TypeId ControlFlowCollector::collectContinue(const rx::ast::ContinueExpr &continuestmt){
    // 确认位于循环内且无需结果值，并将表达式类型记为 never。
}
