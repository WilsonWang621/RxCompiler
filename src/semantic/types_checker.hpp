#pragma once

#include "ast.hpp"
#include "type.hpp"
#include "type_adjustment.hpp"

#include <optional>
#include <unordered_map>
#include <vector>

class SemanticChecker;

// 类型转换、公共类型、trait 和结构体校验。
class TypesChecker{
private:
    SemanticChecker &checker_;

    // 递归重载共享单次查询的缓存，公开入口各自创建缓存。
    bool hasCopy(TypeId type, std::unordered_map<TypeId, bool> &results) const;
    bool hasClone(TypeId type, std::unordered_map<TypeId, bool> &results) const;
    bool hasPartialEq(TypeId type, std::unordered_map<TypeId, bool> &results) const;
    bool hasEq(TypeId type, std::unordered_map<TypeId, bool> &results) const;
    bool checkStructLayout(TypeId type, std::unordered_map<TypeId, bool> &results) const;

public:
    explicit TypesChecker(SemanticChecker &checker) : checker_(checker){}

    // 类型相等、隐式转换与 as 转换是不同规则；合并类型时保留所有先前结果。
    bool canCoerce(TypeId source, TypeId target) const; //判断能否进行隐式转换
    // 由计划生成统一 canCoerce 与 IR 所需的转换规则。
    std::optional<CoercionPlan> planCoercion(TypeId source, TypeId target) const;
    // 成功后按 input.expression 写入模型；失败不记录，由调用方定位诊断。
    bool recordCoercion(const CoercionInput &input, TypeId target);
    // 先确定最终 LUB 并验证全部输入，再批量记录各结果的转换，包含早先结果。
    TypeId collectCommonExpressionType(const std::vector<CoercionInput> &inputs, const rx::ast::ASTNode &node);
    bool canCast(TypeId source, TypeId target) const;  //判断 as 显式转换是否合法
    TypeId collectCommonType(const std::vector<TypeId> &types, const rx::ast::ASTNode &node); //要把多个结果合成一个类型
    bool hasCopy(TypeId type) const;
    bool hasClone(TypeId type) const;
    bool hasPartialEq(TypeId type) const;
    bool hasEq(TypeId type) const;
    void checkStructLayout(const rx::ast::StructItem &structure);
    void checkStructDerive(const rx::ast::StructItem &structure);
};
