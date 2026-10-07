#pragma once

#include<vector>
#include<memory>
#include<string>
#include<iostream>
#include <optional>
namespace rx::ast{

struct ASTNode{
    virtual ~ASTNode() = default;
    virtual void dump(std::ostream &out, int indent = 0) const = 0;
};

class Item : public ASTNode {};

class Stmt : public ASTNode {};

class Expr : public ASTNode {};

using ItemPtr = std::unique_ptr<Item>;
using ExprPtr = std::unique_ptr<Expr>;
using StmtPtr = std::unique_ptr<Stmt>;

class EmptyStmt final : public Stmt {
public:
    void dump(std::ostream &out, int indent = 0) const override;
};

//the top floor
class Crate : public ASTNode{
    std::vector<ItemPtr> items_;

public:
    void addItem(ItemPtr item){
        items_.push_back(std::move(item));
    }

    const auto &items() const { return items_; }

    void dump(std::ostream &out, int indent = 0) const override;
};

class BlockExpr final : public Expr{
    std::vector<StmtPtr> stmts_;
    ExprPtr tail_;
public:
    void addStatement(StmtPtr statement){ 
        stmts_.push_back(std::move(statement));
    }
    
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
    std::unique_ptr<TypeRef> referent_;
    bool isMutable_;

public:
    ReferenceTypeRef(std::unique_ptr<TypeRef> referent, bool isMutable)
        : referent_(std::move(referent)), isMutable_(isMutable) {}

    const TypeRef &referent() const { return *referent_; }
    bool isMutable() const { return isMutable_; }

    void dump(std::ostream &out, int indent = 0) const override;
};

class NamedFunctionParam final : public FunctionParam {
private:
    std::string name_;
    bool isMutable_;
    std::unique_ptr<TypeRef> type_;

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
    bool isReference_;
    bool isMutable_;

public:
    SelfFunctionParam(bool isReference, bool isMutable)
        : isReference_(isReference), isMutable_(isMutable) {}

    bool isReference() const { return isReference_; }
    bool isMutable() const { return isMutable_; }

    void dump(std::ostream &out, int indent = 0) const override;
};

class FunctionItem final : public Item{
private:
    std::string name_;
    std::unique_ptr<SelfFunctionParam> selfParam_;   // nullptr 表示没有 self 参数
    std::vector<std::unique_ptr<FunctionParam>> parameters_;  // 可以为空，表示没有普通参数
    std::unique_ptr<TypeRef> returnType_;  // nullptr 表示没有写 -> 类型
    std::unique_ptr<BlockExpr> body_;
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
    std::string name_;
    std::unique_ptr<TypeRef> type_;
    ExprPtr value_;

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
    std::string name_;
    std::unique_ptr<TypeRef> type_;

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
    std::vector<std::string> names_;

public:
    explicit DeriveAttribute(std::vector<std::string> names)
        : names_(std::move(names)) {}

    const auto &names() const { return names_; }

    void dump(std::ostream &out, int indent = 0) const override;
};

// struct NAME { field: Type, ... }：按源码顺序保存字段，也允许空字段列表。
class StructItem final : public Item {
    std::string name_;
    std::vector<std::unique_ptr<StructField>> fields_;
    std::vector<std::unique_ptr<OuterAttribute>> attributes_;

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
    std::unique_ptr<TypeRef> type_;
    std::vector<ItemPtr> items_;

public:
    ImplItem(std::unique_ptr<TypeRef> type, std::vector<ItemPtr> items)
        : type_(std::move(type)), items_(std::move(items)) {}

    const TypeRef &type() const { return *type_; }
    const auto &items() const { return items_; }

    void dump(std::ostream &out, int indent = 0) const override;
};

//version 1.1
class LetStmt final : public Stmt{
    std::string name_;
    bool isMutable_;
    ExprPtr initializer_;
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
    ExprPtr expression_;
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
    bool flag_;

public:
    BooleanLiteralExpr(bool flag): flag_(flag){};

    bool value() const { return flag_; }

    void dump(std::ostream &out, int indent = 0) const override;
};

//1.2
class BinaryExpr final : public Expr {
private:
    std::string op_;
    ExprPtr left_;
    ExprPtr right_;

public:
    BinaryExpr(std::string op, ExprPtr left, ExprPtr right): op_(std::move(op)), left_(std::move(left)), right_(std::move(right)) {}

    const std::string &op() const { return op_; }
    const Expr &left() const { return *left_; }
    const Expr &right() const { return *right_; }

    void dump(std::ostream &out, int indent = 0) const override;
};

class UnaryExpr final : public Expr{
    std::string op_;  // -、!、*、& 或 &mut；连续借用保存为嵌套节点。
    ExprPtr operand_;

public:
    UnaryExpr(std::string op, ExprPtr operand): op_(std::move(op)), operand_(std::move(operand)){};

    const std::string &op() const { return op_; }
    const Expr &operand() const { return *operand_; }

    void dump(std::ostream &out, int indent = 0) const override;
};

class CastExpr final : public Expr {
    ExprPtr value_;
    std::unique_ptr<TypeRef> type_;  // as 的右侧是类型，不是值表达式。

public:
    CastExpr(ExprPtr value, std::unique_ptr<TypeRef> type)
        : value_(std::move(value)), type_(std::move(type)) {}

    const Expr &value() const { return *value_; }
    const TypeRef &type() const { return *type_; }

    void dump(std::ostream &out, int indent = 0) const override;
};

using GenericArgs = std::vector<std::unique_ptr<TypeRef>>;

// eg.Container::<i32>::new::<u32>()
class PathSegment final : public ASTNode { //每个路径段保存自己的名称和泛型参数，防止 `Container` 和 `new` 的参数混在一起。
    std::string name_;
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
    std::vector<std::unique_ptr<PathSegment>> segments_;

public:
    explicit TypePathRef(std::vector<std::unique_ptr<PathSegment>> segments)
        : segments_(std::move(segments)) {}

    const auto &segments() const { return segments_; }
    void dump(std::ostream &out, int indent = 0) const override;
};

class PathExpr final : public Expr{
    std::vector<std::unique_ptr<PathSegment>> segments_;

public:
    explicit PathExpr(std::vector<std::unique_ptr<PathSegment>> segments)
        : segments_(std::move(segments)) {}

    const auto &segments() const { return segments_; }

    void dump(std::ostream &out, int indent = 0) const override;
};

class StructExprField final : public ASTNode {
    std::string name_;
    ExprPtr value_;

public:
    StructExprField(std::string name, ExprPtr value)
        : name_(std::move(name)), value_(std::move(value)) {}

    const std::string &name() const { return name_; }
    const Expr &value() const { return *value_; }

    void dump(std::ostream &out, int indent = 0) const override;
};

// 路径保留泛型实参；初始化字段按源码顺序保存，供后续确定求值顺序。
class StructExpr final : public Expr {
    std::unique_ptr<PathExpr> path_;
    std::vector<std::unique_ptr<StructExprField>> fields_;

public:
    StructExpr(std::unique_ptr<PathExpr> path, std::vector<std::unique_ptr<StructExprField>> fields)
        : path_(std::move(path)), fields_(std::move(fields)) {}

    const PathExpr &path() const { return *path_; }
    const auto &fields() const { return fields_; }

    void dump(std::ostream &out, int indent = 0) const override;
};

class FieldExpr final : public Expr {
    ExprPtr base_;
    std::string field_;

public:
    FieldExpr(ExprPtr base, std::string field): base_(std::move(base)), field_(std::move(field)) {}

    const Expr &base() const { return *base_; }
    const std::string &field() const { return field_; }

    void dump(std::ostream &out, int indent = 0) const override;
};

class MethodCallExpr final : public Expr {
    ExprPtr receiver_;
    std::unique_ptr<PathSegment> method_;
    std::vector<ExprPtr> arguments_;

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
    ExprPtr target_;
    ExprPtr value_;

public:
    AssignExpr(ExprPtr target, ExprPtr value): target_(std::move(target)), value_(std::move(value)) {}

    const Expr &target() const { return *target_; }
    const Expr &value() const { return *value_; }

    void dump(std::ostream &out, int indent = 0) const override;
};

// 保留 += 等原始操作，避免改写成赋值加二元运算后重复求值 target。
class CompoundAssignExpr final : public Expr {
    std::string op_;
    ExprPtr target_;
    ExprPtr value_;

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
    ExprPtr condition_;
    std::unique_ptr<BlockExpr> thenBranch_;
    ExprPtr elseBranch_;

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
    std::unique_ptr<BlockExpr> block_;
public:
    explicit LoopExpr(std::unique_ptr<BlockExpr> block): block_(std::move(block)){};

    const BlockExpr &body() const { return *block_; }

    void dump(std::ostream &out, int indent = 0) const override;
};

// while condition { ... }：节点分别持有条件表达式和循环体。
class WhileExpr final : public Expr{
private:
    ExprPtr condition_;
    std::unique_ptr<BlockExpr> block_;
public:
    WhileExpr(ExprPtr condition, std::unique_ptr<BlockExpr> block): condition_(std::move(condition)), block_(std::move(block)){};

    const Expr &condition() const { return *condition_; }
    const BlockExpr &body() const { return *block_; }

    void dump(std::ostream &out, int indent = 0) const override;
};


class ArrayExpr final : public Expr{
private:
    std::vector<ExprPtr> elements_;

public:
    ArrayExpr(std::vector<ExprPtr> elements) : elements_(std::move(elements)){};

    const auto &elements() const { return elements_; }

    void dump(std::ostream &out, int indent = 0) const override;
};

class ArrayRepeatExpr final : public Expr{
private:
    ExprPtr value_;
    ExprPtr count_;   // 保存次数的常量表达式，后续语义分析再求值
public:
    ArrayRepeatExpr(ExprPtr value, ExprPtr count) : value_(std::move(value)), count_(std::move(count)){};

    const Expr &value() const { return *value_; }
    const Expr &count() const { return *count_; }

    void dump(std::ostream &out, int indent = 0) const override;
};

class IndexExpr final : public Expr{
private:
    ExprPtr base_;
    ExprPtr index_;
public:
    IndexExpr(ExprPtr base, ExprPtr index) : base_(std::move(base)), index_(std::move(index)){};

    const Expr &base() const { return *base_; }
    const Expr &index() const { return *index_; }

    void dump(std::ostream &out, int indent = 0) const override;
};
}
