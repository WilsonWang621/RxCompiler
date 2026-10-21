#include "expression_collector.hpp"
#include "semantic_checker.hpp"

TypeId ExpressionCollector::collectExpr(const rx::ast::Expr &expression, std::optional<TypeId> expectedType){
    // 按具体 AST 类型分派检查，记录 expressionTypes 和 place、可变性等 ExprInfo。
}

TypeId ExpressionCollector::collectUnit(const rx::ast::UnitExpr &expression){
    // 将 unit 表达式标记为非 place，并返回内建 Unit 类型。
}

TypeId ExpressionCollector::collectIntegerLiteral(const rx::ast::IntegerLiteralExpr &expression, std::optional<TypeId> expectedType){
    // 解析进制、下划线和后缀，按后缀、预期整数类型、i32 默认值的顺序确定类型。
}

TypeId ExpressionCollector::collectBooleanLiteral(const rx::ast::BooleanLiteralExpr &expression){
    // 将布尔字面量标记为非 place，并返回内建 Bool 类型。
}

TypeId ExpressionCollector::collectPath(const rx::ast::PathExpr &path){
    // 解析路径符号，取得常量或绑定的类型，并记录该表达式是否为可变 place。
}

TypeId ExpressionCollector::collectBinaryExpr(const rx::ast::BinaryExpr &expression){
    // 按运算符检查左右操作数类型、短路规则和比较能力，确定运算结果类型。
}

TypeId ExpressionCollector::collectUnaryExpr(const rx::ast::UnaryExpr &expression){
    // 检查负号、逻辑非、借用和解引用的操作数约束，并计算结果类型及 place 属性。
}

TypeId ExpressionCollector::collectCast(const rx::ast::CastExpr &expression){
    // 解析目标类型，检查源类型是否允许显式 as 转换，并返回目标类型。
}

TypeId ExpressionCollector::collectAssignment(const rx::ast::AssignExpr &assignment){
    // 检查左侧为可变 place，以其类型检查右侧的求值和隐式转换，结果为 unit。
}

TypeId ExpressionCollector::collectCompoundAssignment(const rx::ast::CompoundAssignExpr &assignment){
    // 检查目标可变性及对应二元运算约束，保证目标只求值一次，结果为 unit。
}

TypeId ExpressionCollector::collectArray(const rx::ast::ArrayExpr &array, std::optional<TypeId> expectedType){
    // 检查各元素，使用预期数组元素类型或 LUB 合并类型，并以元素数构造数组类型。
}

TypeId ExpressionCollector::collectRepeatedArray(const rx::ast::ArrayRepeatExpr &repeatedArray, std::optional<TypeId> expectedType){
    // 检查重复值类型、常量长度及复制要求，并构造对应的固定数组类型。
}

TypeId ExpressionCollector::collectIndex(const rx::ast::IndexExpr &expression){
    // 检查基值可索引且索引为 usize，返回元素类型并继承适当的 place 和可变性。
}

TypeId ExpressionCollector::collectStructExpr(const rx::ast::StructExpr &expression){
    // 解析结构体，检查字段无重复、无遗漏且初始化值匹配声明类型，返回结构体类型。
}

TypeId ExpressionCollector::collectField(const rx::ast::FieldExpr &expression){
    // 根据基表达式类型查找字段，返回字段类型并传播基 place 的可变访问能力。
}

TypeId ExpressionCollector::collectCall(const rx::ast::CallExpr &expression){
    // 解析调用目标，按函数签名检查实参数量、求值顺序和类型，返回函数结果类型。
}

TypeId ExpressionCollector::collectMethodCall(const rx::ast::MethodCallExpr &expression){
    // 根据接收者类型查找方法，执行允许的自动借用或解引用，并检查其余实参。
}

bool ExpressionCollector::checkMutablePlace(const rx::ast::Expr &expression){
    // 查询表达式 place 信息，诊断不可赋值或不可变目标，并报告是否允许写入。
}
