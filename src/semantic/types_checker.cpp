#include "types_checker.hpp"
#include "semantic_checker.hpp"

bool TypesChecker::canCoerce(TypeId source, TypeId target) const{
    // 判断恒等、never、引用弱化以及规范允许的重复解引用再借用转换是否成立。
}

bool TypesChecker::canCast(TypeId source, TypeId target) const{
    // 判断源和目标是否属于 Rx 允许的整数互转或 bool 到整数显式转换。
}

TypeId TypesChecker::collectCommonType(const std::vector<TypeId> &types, const rx::ast::ASTNode &node){
    // 忽略 never 后按源码顺序执行 LUB 转换，失败时诊断并返回 Error 类型。
}

bool TypesChecker::hasCopy(TypeId type) const{
    // 按内建类型、复合元素和结构体 derive 信息递归判断类型是否实现 Copy。
}

bool TypesChecker::hasClone(TypeId type) const{
    // 按内建类型、容器元素和结构体 derive 信息递归判断类型是否实现 Clone。
}

bool TypesChecker::hasPartialEq(TypeId type) const{
    // 按标量、复合元素和结构体 derive 信息递归判断类型是否支持相等比较。
}

bool TypesChecker::hasEq(TypeId type) const{
    // 按标量、复合元素和结构体 derive 信息递归判断类型是否实现完整 Eq。
}

void TypesChecker::checkStructLayout(const rx::ast::StructItem &structure){
    // 沿按值结构体和数组字段检测无限大小递归；引用、Box 和 Vec 截断递归路径。
}

void TypesChecker::checkStructDerive(const rx::ast::StructItem &structure){
    // 校验 derive 名称、重复项和依赖关系，并确认所有字段满足派生 trait 的能力要求。
}
