#pragma once

#include<vector>
#include<memory>
#include<string>
#include<iostream>
#include <optional>
namespace rx::ast{

// 所有节点共用虚析构和 dump 接口，允许通过基类指针持有和输出具体节点。
struct ASTNode{
    virtual ~ASTNode() = default;
    virtual void dump(std::ostream &out, int indent = 0) const = 0;
};

class Item : public ASTNode {};

class Stmt : public ASTNode {};

class Expr : public ASTNode {};

// 父节点独占子节点；构造函数用 std::move 接收所有权，访问器只提供只读视图。
// 可选子节点的访问器返回指针，调用方需要先检查 nullptr；必需子节点返回引用。
using ItemPtr = std::unique_ptr<Item>;
using ExprPtr = std::unique_ptr<Expr>;
using StmtPtr = std::unique_ptr<Stmt>;

// 单独的分号形成空语句，不等同于单元值表达式 ()。
class EmptyStmt final : public Stmt {
public:
    void dump(std::ostream &out, int indent = 0) const override;
};

//the top floor
class Crate : public ASTNode{
    std::vector<ItemPtr> items_;  // 按源码顺序持有顶层声明；解析后丢弃的 use 不在其中。

public:
    // 接收一个声明节点的所有权并追加到顶层列表。
    void addItem(ItemPtr item){
        items_.push_back(std::move(item));
    }

    const auto &items() const { return items_; }

    void dump(std::ostream &out, int indent = 0) const override;
};

class BlockExpr final : public Expr{
    std::vector<StmtPtr> stmts_;  // 块内的语句序列，不包含用于产生块结果的尾表达式。
    ExprPtr tail_;  // nullptr 表示没有尾表达式；有值时保存末尾无分号的表达式。
public:
    // 语句按出现顺序追加，后续阶段按此顺序处理。
    void addStatement(StmtPtr statement){ 
        stmts_.push_back(std::move(statement));
    }
    
    // 尾表达式单独保存，其结果决定正常结束时的块结果。
    void setTail(ExprPtr tail){
        tail_ = std::move(tail);
    }

    const auto &statements() const { return stmts_; }
    const Expr *tail() const { return tail_.get(); }

    void dump(std::ostream &out, int indent = 0) const override;
};

class FunctionParam : public ASTNode {
public:
    ~FunctionParam() override = default;
};

// 类型共用一个基类，参数、返回值和变量注解都持有 unique_ptr<TypeRef>。
class TypeRef : public ASTNode {};

// () 是单元类型；命名类型和类型实参由 TypePathRef 结构化保存。
class UnitTypeRef final : public TypeRef {
public:
    void dump(std::ostream &out, int indent = 0) const override;
};

class ArrayTypeRef final : public TypeRef {
    std::unique_ptr<TypeRef> elementType_;  // 内层也可以是数组类型，表示多维数组。
    ExprPtr count_;  // 保存 3、N 等表达式；长度求值与合法性检查留给语义分析。

public:
    ArrayTypeRef(std::unique_ptr<TypeRef> elementType, ExprPtr count)
        : elementType_(std::move(elementType)), count_(std::move(count)) {}

    const TypeRef &elementType() const { return *elementType_; }
    const Expr &count() const { return *count_; }

    void dump(std::ostream &out, int indent = 0) const override;
};

class ReferenceTypeRef final : public TypeRef {
    std::unique_ptr<TypeRef> referent_;    // 被引用的类型 T
    bool isMutable_;  // 当前这一层是否为 &mut；多层引用通过嵌套节点表示。

public:
    ReferenceTypeRef(std::unique_ptr<TypeRef> referent, bool isMutable)
        : referent_(std::move(referent)), isMutable_(isMutable) {}

    const TypeRef &referent() const { return *referent_; }
    bool isMutable() const { return isMutable_; }

    void dump(std::ostream &out, int indent = 0) const override;
};

class NamedFunctionParam final : public FunctionParam {
private:
    std::string name_;  // 参数绑定的名称。
    bool isMutable_;  // mut x 中绑定是否可变，与参数类型是否为 &mut 独立。
    std::unique_ptr<TypeRef> type_;  // 文法要求普通参数显式声明类型。

public:
    NamedFunctionParam(std::string name, bool isMutable, std::unique_ptr<TypeRef> type)
        : name_(std::move(name)), isMutable_(isMutable), type_(std::move(type)) {}

    const std::string &name() const { return name_; }
    bool isMutable() const { return isMutable_; }
    const TypeRef &type() const { return *type_; }

    void dump(std::ostream &out, int indent = 0) const override;
};

class SelfFunctionParam final : public FunctionParam {
private:
    bool isReference_;  // true 表示 &self 或 &mut self，false 表示按值接收 self。
    bool isMutable_;  // 有引用时表示 &mut self，否则表示 mut self。

public:
    SelfFunctionParam(bool isReference, bool isMutable)
        : isReference_(isReference), isMutable_(isMutable) {}

    bool isReference() const { return isReference_; }
    bool isMutable() const { return isMutable_; }

    void dump(std::ostream &out, int indent = 0) const override;
};

class FunctionItem final : public Item{
private:
    std::string name_;  // 函数或关联方法的声明名称。
    std::unique_ptr<SelfFunctionParam> selfParam_;   // nullptr 表示没有 self 参数
    std::vector<std::unique_ptr<FunctionParam>> parameters_;  // 可以为空，表示没有普通参数
    std::unique_ptr<TypeRef> returnType_;  // nullptr 表示没有写 -> 类型
    std::unique_ptr<BlockExpr> body_;  // 函数体，包含语句和可选尾表达式。
    bool hasGenericParameters_;  // main 禁止声明泛型形参；无需保留生命周期名称和约束。
public:
    FunctionItem(
        std::string name,
        std::unique_ptr<SelfFunctionParam> selfParam,
        std::vector<std::unique_ptr<FunctionParam>> parameters,
        std::unique_ptr<TypeRef> returnType,
        std::unique_ptr<BlockExpr> body,
        bool hasGenericParameters = false
    ): name_(std::move(name)), selfParam_(std::move(selfParam)), parameters_(std::move(parameters)), returnType_(std::move(returnType)), body_(std::move(body)), hasGenericParameters_(hasGenericParameters) {}

    const std::string &name() const { return name_; }
    const SelfFunctionParam *selfParam() const { return selfParam_.get(); }
    const auto &parameters() const { return parameters_; }
    const TypeRef *returnType() const { return returnType_.get(); }
    const BlockExpr &body() const { return *body_; }
    bool hasGenericParameters() const { return hasGenericParameters_; }

    void dump(std::ostream &out, int indent) const override;
};

// const NAME: Type = value;：保存声明类型和未求值的常量表达式。
class ConstItem final : public Item {
    std::string name_;  // 常量声明名称。
    std::unique_ptr<TypeRef> type_;  // const 必须显式声明的类型。
    ExprPtr value_;  // 初始化表达式，尚未进行常量求值。

public:
    ConstItem(std::string name, std::unique_ptr<TypeRef> type, ExprPtr value)
        : name_(std::move(name)), type_(std::move(type)), value_(std::move(value)) {}

    const std::string &name() const { return name_; }
    const TypeRef &type() const { return *type_; }
    const Expr &value() const { return *value_; }

    void dump(std::ostream &out, int indent = 0) const override;
};

// 字段类型复用 TypeRef；字段重名和类型是否合法留给语义分析。
class StructField final : public ASTNode {
    std::string name_;  // 字段声明名称，用于后续字段查找和重名检查。
    std::unique_ptr<TypeRef> type_;  // 字段的声明类型，可嵌套引用、数组和类型路径。

public:
    StructField(std::string name, std::unique_ptr<TypeRef> type)
        : name_(std::move(name)), type_(std::move(type)) {}

    const std::string &name() const { return name_; }
    const TypeRef &type() const { return *type_; }

    void dump(std::ostream &out, int indent = 0) const override;
};

class OuterAttribute : public ASTNode {};

// #[derive(...)]：保留每个属性的边界，以及名称的顺序、重复和空列表。
class DeriveAttribute final : public OuterAttribute {
    std::vector<std::string> names_;  // 此次 derive 括号内的名称，不去重也不合并其他属性。

public:
    explicit DeriveAttribute(std::vector<std::string> names)
        : names_(std::move(names)) {}

    const auto &names() const { return names_; }

    void dump(std::ostream &out, int indent = 0) const override;
};

// struct NAME { field: Type, ... }：按源码顺序保存字段，也允许空字段列表。
class StructItem final : public Item {
    std::string name_;  // 结构体的声明名称。
    std::vector<std::unique_ptr<StructField>> fields_;  // 独占字段节点，空列表表示空结构体。
    std::vector<std::unique_ptr<OuterAttribute>> attributes_;  // 按顺序保留各个外部属性。

public:
    StructItem(std::string name, std::vector<std::unique_ptr<StructField>> fields,
               std::vector<std::unique_ptr<OuterAttribute>> attributes = {})
        : name_(std::move(name)), fields_(std::move(fields)), attributes_(std::move(attributes)) {}

    const std::string &name() const { return name_; }
    const auto &fields() const { return fields_; }
    const auto &attributes() const { return attributes_; }

    void dump(std::ostream &out, int indent = 0) const override;
};

// impl 是独立的声明节点，按源码顺序保存方法和关联常量。
class ImplItem final : public Item {
    std::unique_ptr<TypeRef> type_;  // impl 后面的目标类型，是否合法由语义分析判断。
    std::vector<ItemPtr> items_;  // 关联函数和关联常量，保留各声明的源码顺序。

public:
    ImplItem(std::unique_ptr<TypeRef> type, std::vector<ItemPtr> items)
        : type_(std::move(type)), items_(std::move(items)) {}

    const TypeRef &type() const { return *type_; }
    const auto &items() const { return items_; }

    void dump(std::ostream &out, int indent = 0) const override;
};

//version 1.1
class LetStmt final : public Stmt{
    std::string name_;  // 局部变量绑定名称。
    bool isMutable_;  // let mut 是否允许后续修改此绑定。
    ExprPtr initializer_;  // 当前文法要求存在初始化表达式。
    std::unique_ptr<TypeRef> type_;  // nullptr 表示省略类型注解，后续需要类型推导。

public:
    LetStmt(std::string name, bool isMutable, ExprPtr initializer,
            std::unique_ptr<TypeRef> type = nullptr)
        : name_(std::move(name)), isMutable_(isMutable), initializer_(std::move(initializer)),
          type_(std::move(type)) {}

    const std::string &name() const { return name_; }
    bool isMutable() const { return isMutable_; }
    const Expr &initializer() const { return *initializer_; }
    const TypeRef *type() const { return type_.get(); }

    void dump(std::ostream &out, int indent = 0) const override;
};

class ExprStmt final : public Stmt {
    ExprPtr expression_;  // 作为语句使用的表达式，与块尾表达式分开保存。
    bool hasSemicolon_;  // 非末尾的带块语句省略分号时，需要检查结果为 () 或 never。

public:
    explicit ExprStmt(ExprPtr expression, bool hasSemicolon = true)
        : expression_(std::move(expression)), hasSemicolon_(hasSemicolon) {}

    bool hasSemicolon() const { return hasSemicolon_; }

    const Expr &expression() const { return *expression_; }

    void dump(std::ostream &out, int indent = 0) const override;
};

// () 是单元值表达式；与空语句及类型注解中的 () 分开表示。
class UnitExpr final : public Expr {
public:
    void dump(std::ostream &out, int indent = 0) const override;
};

class IntegerLiteralExpr final : public Expr{
    std::string text_;  // 完整字面量原文，包括进制前缀、下划线和类型后缀。

public:
    IntegerLiteralExpr(std::string text):text_(std::move(text)){};

    const std::string &text() const { return text_; }

    void dump(std::ostream &out, int indent = 0) const override;
};

class BooleanLiteralExpr final : public Expr{
    bool flag_;  // true / false 字面量对应的布尔值。

public:
    BooleanLiteralExpr(bool flag): flag_(flag){};

    bool value() const { return flag_; }

    void dump(std::ostream &out, int indent = 0) const override;
};

//1.2
class BinaryExpr final : public Expr {
private:
    std::string op_;  // 二元运算符原文；优先级和结合顺序由子树结构表达。
    ExprPtr left_;  // 左操作数。
    ExprPtr right_;  // 右操作数；逻辑运算的短路行为留给后续阶段处理。

public:
    BinaryExpr(std::string op, ExprPtr left, ExprPtr right): op_(std::move(op)), left_(std::move(left)), right_(std::move(right)) {}

    const std::string &op() const { return op_; }
    const Expr &left() const { return *left_; }
    const Expr &right() const { return *right_; }

    void dump(std::ostream &out, int indent = 0) const override;
};

class UnaryExpr final : public Expr{
    std::string op_;  // -、!、*、& 或 &mut；连续借用保存为嵌套节点。
    ExprPtr operand_;  // 前缀运算符作用的表达式，可继续嵌套一元运算。

public:
    UnaryExpr(std::string op, ExprPtr operand): op_(std::move(op)), operand_(std::move(operand)){};

    const std::string &op() const { return op_; }
    const Expr &operand() const { return *operand_; }

    void dump(std::ostream &out, int indent = 0) const override;
};

class CastExpr final : public Expr {
    ExprPtr value_;  // as 左侧待转换的值表达式。
    std::unique_ptr<TypeRef> type_;  // as 的右侧是类型，不是值表达式。

public:
    CastExpr(ExprPtr value, std::unique_ptr<TypeRef> type)
        : value_(std::move(value)), type_(std::move(type)) {}

    const Expr &value() const { return *value_; }
    const TypeRef &type() const { return *type_; }

    void dump(std::ostream &out, int indent = 0) const override;
};

// 泛型实参只保留具体类型节点；生命周期实参在构建时丢弃。
using GenericArgs = std::vector<std::unique_ptr<TypeRef>>;

// eg.Container::<i32>::new::<u32>()
class PathSegment final : public ASTNode { //每个路径段保存自己的名称和泛型参数，防止 `Container` 和 `new` 的参数混在一起。
    std::string name_;  // 当前路径段的名称，也可能是 self 或 Self。
    // nullopt 表示省略参数；空列表表示显式写了 ::<>。
    std::optional<GenericArgs> genericArgs_;

public:
    explicit PathSegment(std::string name,
                         std::optional<GenericArgs> genericArgs = std::nullopt)
        : name_(std::move(name)), genericArgs_(std::move(genericArgs)) {}

    const std::string &name() const { return name_; }
    bool hasGenericArgs() const { return genericArgs_.has_value(); }

    const auto &genericArgs() const { return genericArgs_; }

    void dump(std::ostream &out, int indent = 0) const override;
};

// 类型路径复用路径段；嵌套引用、数组和容器实参均保存为 TypeRef。
class TypePathRef final : public TypeRef {
    std::vector<std::unique_ptr<PathSegment>> segments_;  // 以 :: 分隔的类型路径段。

public:
    explicit TypePathRef(std::vector<std::unique_ptr<PathSegment>> segments)
        : segments_(std::move(segments)) {}

    const auto &segments() const { return segments_; }
    void dump(std::ostream &out, int indent = 0) const override;
};

class PathExpr final : public Expr{
    std::vector<std::unique_ptr<PathSegment>> segments_;  // 值位置的路径，名称解析留给语义分析。

public:
    explicit PathExpr(std::vector<std::unique_ptr<PathSegment>> segments)
        : segments_(std::move(segments)) {}

    const auto &segments() const { return segments_; }

    void dump(std::ostream &out, int indent = 0) const override;
};

class StructExprField final : public ASTNode {
    std::string name_;  // 构造表达式中 field: value 的字段名称。
    ExprPtr value_;  // 该字段的初始化表达式，与字段声明类型分开表示。

public:
    StructExprField(std::string name, ExprPtr value)
        : name_(std::move(name)), value_(std::move(value)) {}

    const std::string &name() const { return name_; }
    const Expr &value() const { return *value_; }

    void dump(std::ostream &out, int indent = 0) const override;
};

// 路径保留泛型实参；初始化字段按源码顺序保存，供后续确定求值顺序。
class StructExpr final : public Expr {
    std::unique_ptr<PathExpr> path_;  // 被构造结构体的路径，含各段具体类型实参。
    std::vector<std::unique_ptr<StructExprField>> fields_;  // 字段初始化列表；S {} 时为空。

public:
    StructExpr(std::unique_ptr<PathExpr> path, std::vector<std::unique_ptr<StructExprField>> fields)
        : path_(std::move(path)), fields_(std::move(fields)) {}

    const PathExpr &path() const { return *path_; }
    const auto &fields() const { return fields_; }

    void dump(std::ostream &out, int indent = 0) const override;
};

class FieldExpr final : public Expr {
    ExprPtr base_;  // 点号左侧被访问的表达式，如 a.b 中的 a。
    std::string field_;  // 点号右侧的字段名称，不包含方法调用信息。

public:
    FieldExpr(ExprPtr base, std::string field): base_(std::move(base)), field_(std::move(field)) {}

    const Expr &base() const { return *base_; }
    const std::string &field() const { return field_; }

    void dump(std::ostream &out, int indent = 0) const override;
};

class MethodCallExpr final : public Expr {
    ExprPtr receiver_;  // 点号左侧的接收者，后续进行方法查找及自动借用或解引用。
    std::unique_ptr<PathSegment> method_;  // 方法名称及其显式具体类型实参。
    std::vector<ExprPtr> arguments_;  // 括号内的显式参数，不包含 receiver。

public:
    MethodCallExpr(ExprPtr receiver, std::unique_ptr<PathSegment> method, std::vector<ExprPtr> arguments)
        : receiver_(std::move(receiver)), method_(std::move(method)), arguments_(std::move(arguments)) {}

    const Expr &receiver() const { return *receiver_; }
    const PathSegment &method() const { return *method_; }
    const auto &arguments() const { return arguments_; }

    void dump(std::ostream &out, int indent = 0) const override;
};

class AssignExpr final : public Expr {
private:
    ExprPtr target_;  // 等号左侧的目标，是否可赋值由语义分析检查。
    ExprPtr value_;  // 等号右侧的表达式，也可以是嵌套赋值。

public:
    AssignExpr(ExprPtr target, ExprPtr value): target_(std::move(target)), value_(std::move(value)) {}

    const Expr &target() const { return *target_; }
    const Expr &value() const { return *value_; }

    void dump(std::ostream &out, int indent = 0) const override;
};

// 保留 += 等原始操作，避免改写成赋值加二元运算后重复求值 target。
class CompoundAssignExpr final : public Expr {
    std::string op_;  // +=、-= 等完整复合赋值运算符。
    ExprPtr target_;  // 保留原始目标表达式，后续生成代码时只求值一次。
    ExprPtr value_;  // 参与复合运算的右侧表达式。

public:
    CompoundAssignExpr(std::string op, ExprPtr target, ExprPtr value)
        : op_(std::move(op)), target_(std::move(target)), value_(std::move(value)) {}

    const std::string &op() const { return op_; }
    const Expr &target() const { return *target_; }
    const Expr &value() const { return *value_; }

    void dump(std::ostream &out, int indent = 0) const override;
};

class IfExpr final : public Expr {
private:
    ExprPtr condition_;  // if 后面的条件表达式，布尔类型检查留给语义分析。
    std::unique_ptr<BlockExpr> thenBranch_;  // 条件成立时执行的块。
    ExprPtr elseBranch_;  // nullptr 表示无 else；否则为块或嵌套的 IfExpr。

public:
    IfExpr(ExprPtr condition, std::unique_ptr<BlockExpr> thenBranch, ExprPtr elseBranch): condition_(std::move(condition)), thenBranch_(std::move(thenBranch)), elseBranch_(std::move(elseBranch)) {}

    const Expr &condition() const { return *condition_; }
    const BlockExpr &thenBranch() const { return *thenBranch_; }
    const Expr *elseBranch() const { return elseBranch_.get(); }

    void dump(std::ostream &out, int indent = 0) const override;
};

class CallExpr final : public Expr{
    // 被调用者可以是任意表达式，例如 f()(x) 或 a[i](x) 中前半部分。
    ExprPtr callee_;
    std::vector<ExprPtr> arguments_;  // 按源码顺序保存；f() 的参数列表为空。

public:
    CallExpr(ExprPtr callee, std::vector<ExprPtr> arguments)
        : callee_(std::move(callee)), arguments_(std::move(arguments)) {}

    const Expr &callee() const { return *callee_; }
    const auto &arguments() const { return arguments_; }

    void dump(std::ostream &out, int indent = 0) const override;
};

class ReturnExpr final : public Expr{  //occur twice in parser.g4 nonblockPrimary & conditionPrimaryWithoutBareBlock 
private:
    ExprPtr value_;   //nullptr : return;
public:
    explicit ReturnExpr(ExprPtr value): value_(std::move(value)){};

    const Expr *value() const { return value_.get(); }

    void dump(std::ostream &out, int indent = 0) const override;
};

// 与 return 一样，break 和 continue 都作为表达式节点构建。
// 循环归属和 break 值的合法性留给语义分析判断。
class BreakExpr final : public Expr{
private:
    ExprPtr value_;   // nullptr 表示 break;，否则持有 break 后面的值。
public:
    explicit BreakExpr(ExprPtr value): value_(std::move(value)){};

    const Expr *value() const { return value_.get(); }

    void dump(std::ostream &out, int indent = 0) const override;
};

class ContinueExpr final : public Expr{
public:
    void dump(std::ostream &out, int indent = 0) const override;
};

// loop { ... }：只持有循环体，退出由循环体中的 break 表达式表示。
class LoopExpr final : public Expr{
private:
    std::unique_ptr<BlockExpr> block_;  // loop 的循环体，保留其语句和尾表达式。
public:
    explicit LoopExpr(std::unique_ptr<BlockExpr> block): block_(std::move(block)){};

    const BlockExpr &body() const { return *block_; }

    void dump(std::ostream &out, int indent = 0) const override;
};

// while condition { ... }：节点分别持有条件表达式和循环体。
class WhileExpr final : public Expr{
private:
    ExprPtr condition_;  // 每轮循环开始前求值的条件表达式。
    std::unique_ptr<BlockExpr> block_;  // 条件成立时执行的循环体。
public:
    WhileExpr(ExprPtr condition, std::unique_ptr<BlockExpr> block): condition_(std::move(condition)), block_(std::move(block)){};

    const Expr &condition() const { return *condition_; }
    const BlockExpr &body() const { return *block_; }

    void dump(std::ostream &out, int indent = 0) const override;
};


class ArrayExpr final : public Expr{
private:
    std::vector<ExprPtr> elements_;  // [a, b, ...] 中按顺序保存的元素；[] 时为空。

public:
    ArrayExpr(std::vector<ExprPtr> elements) : elements_(std::move(elements)){};

    const auto &elements() const { return elements_; }

    void dump(std::ostream &out, int indent = 0) const override;
};

class ArrayRepeatExpr final : public Expr{
private:
    ExprPtr value_;  // [value; count] 中待重复的元素表达式，不在 AST 阶段展开。
    ExprPtr count_;   // 保存次数的常量表达式，后续语义分析再求值
public:
    ArrayRepeatExpr(ExprPtr value, ExprPtr count) : value_(std::move(value)), count_(std::move(count)){};

    const Expr &value() const { return *value_; }
    const Expr &count() const { return *count_; }

    void dump(std::ostream &out, int indent = 0) const override;
};

class IndexExpr final : public Expr{
private:
    ExprPtr base_;  // 被索引的表达式，支持 a[i][j] 这类嵌套索引。
    ExprPtr index_;  // 方括号内的下标表达式，类型及边界检查留给后续阶段。
public:
    IndexExpr(ExprPtr base, ExprPtr index) : base_(std::move(base)), index_(std::move(index)){};

    const Expr &base() const { return *base_; }
    const Expr &index() const { return *index_; }

    void dump(std::ostream &out, int indent = 0) const override;
};
}
