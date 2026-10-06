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
    std::vector<ItemPtr> items;

public:
    void addItem(ItemPtr item){
        items.push_back(std::move(item));
    }

    void dump(std::ostream &out, int indent = 0) const override;
};

// use 路径不含泛型参数，逐段保存名称和开头的 ::，供后续名称解析使用。
class UsePath final : public ASTNode {
    bool isAbsolute_;
    std::vector<std::string> segments_;

public:
    UsePath(bool isAbsolute, std::vector<std::string> segments)
        : isAbsolute_(isAbsolute), segments_(std::move(segments)) {}

    void dump(std::ostream &out, int indent = 0) const override;
};

class UseTree : public ASTNode {
protected:
    // nullptr 表示没有前缀；::* 和 ::{} 使用绝对路径的空段列表。
    std::unique_ptr<UsePath> path_;

    explicit UseTree(std::unique_ptr<UsePath> path) : path_(std::move(path)) {}
};

// path 或 path as name；nullopt 表示没有 as，"_" 表示匿名导入。
class NamedUseTree final : public UseTree {
    std::optional<std::string> alias_;

public:
    NamedUseTree(std::unique_ptr<UsePath> path, std::optional<std::string> alias)
        : UseTree(std::move(path)), alias_(std::move(alias)) {}

    void dump(std::ostream &out, int indent = 0) const override;
};

class GlobUseTree final : public UseTree {
public:
    explicit GlobUseTree(std::unique_ptr<UsePath> path) : UseTree(std::move(path)) {}

    void dump(std::ostream &out, int indent = 0) const override;
};

// 不展开或合并嵌套导入，保留空分组、分组边界和子树的源码顺序。
class GroupUseTree final : public UseTree {
    std::vector<std::unique_ptr<UseTree>> trees_;

public:
    GroupUseTree(std::unique_ptr<UsePath> path, std::vector<std::unique_ptr<UseTree>> trees)
        : UseTree(std::move(path)), trees_(std::move(trees)) {}

    void dump(std::ostream &out, int indent = 0) const override;
};

class UseItem final : public Item {
    std::unique_ptr<UseTree> tree_;

public:
    explicit UseItem(std::unique_ptr<UseTree> tree) : tree_(std::move(tree)) {}

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

    void dump(std::ostream &out, int indent = 0) const override;
};

class FunctionParam : public ASTNode {
public:
    ~FunctionParam() override = default;
};

// 类型共用一个基类，参数、返回值和变量注解都持有 unique_ptr<TypeRef>。
class TypeRef : public ASTNode {};

// 类型路径与 () 沿用文本表示；数组类型单独保存内部结构。
class SimpleTypeRef final : public TypeRef {
private:
    std::string type_;
public:
    explicit SimpleTypeRef(std::string type) : type_(std::move(type)) {}

    void dump(std::ostream &out, int indent = 0) const override;
};

class ArrayTypeRef final : public TypeRef {
    std::unique_ptr<TypeRef> elementType_;  // 内层也可以是数组类型，表示多维数组。
    ExprPtr count_;  // 保存 3、N 等表达式；长度求值与合法性检查留给语义分析。

public:
    ArrayTypeRef(std::unique_ptr<TypeRef> elementType, ExprPtr count)
        : elementType_(std::move(elementType)), count_(std::move(count)) {}

    void dump(std::ostream &out, int indent = 0) const override;
};

class ReferenceTypeRef final : public TypeRef {
    std::unique_ptr<TypeRef> referent_;
    bool isMutable_;
    std::optional<std::string> lifetime_;

public:
    ReferenceTypeRef(std::unique_ptr<TypeRef> referent, bool isMutable,
                     std::optional<std::string> lifetime = std::nullopt)
        : referent_(std::move(referent)), isMutable_(isMutable), lifetime_(std::move(lifetime)) {}

    void dump(std::ostream &out, int indent = 0) const override;
};

// nullopt 表示省略冒号；空列表表示显式写了 'a: 而没有约束。
class LifetimeParam final : public ASTNode {
    std::string lifetime_;
    std::optional<std::vector<std::string>> bounds_;

public:
    LifetimeParam(std::string lifetime, std::optional<std::vector<std::string>> bounds)
        : lifetime_(std::move(lifetime)), bounds_(std::move(bounds)) {}

    void dump(std::ostream &out, int indent = 0) const override;
};

// 当前文法中的泛型形参只有生命周期；显式 <> 保存为空节点。
class GenericParams final : public ASTNode {
    std::vector<std::unique_ptr<LifetimeParam>> lifetimes_;

public:
    explicit GenericParams(std::vector<std::unique_ptr<LifetimeParam>> lifetimes)
        : lifetimes_(std::move(lifetimes)) {}

    void dump(std::ostream &out, int indent = 0) const override;
};

// where 子句的两类约束分别保存生命周期或类型，不在 AST 构建时验证约束。
class WherePredicate : public ASTNode {};

class LifetimeWherePredicate final : public WherePredicate {
    std::string lifetime_;
    std::vector<std::string> bounds_;

public:
    LifetimeWherePredicate(std::string lifetime, std::vector<std::string> bounds)
        : lifetime_(std::move(lifetime)), bounds_(std::move(bounds)) {}

    void dump(std::ostream &out, int indent = 0) const override;
};

class TypeWherePredicate final : public WherePredicate {
    std::unique_ptr<TypeRef> type_;
    std::vector<std::string> bounds_;

public:
    TypeWherePredicate(std::unique_ptr<TypeRef> type, std::vector<std::string> bounds)
        : type_(std::move(type)), bounds_(std::move(bounds)) {}

    void dump(std::ostream &out, int indent = 0) const override;
};

class WhereClause final : public ASTNode {
    std::vector<std::unique_ptr<WherePredicate>> predicates_;

public:
    explicit WhereClause(std::vector<std::unique_ptr<WherePredicate>> predicates)
        : predicates_(std::move(predicates)) {}

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

    void dump(std::ostream &out, int indent = 0) const override;
};

class SelfFunctionParam final : public FunctionParam {
private:
    bool isReference_;
    bool isMutable_;
    std::optional<std::string> lifetime_;

public:
    SelfFunctionParam(
        bool isReference,
        bool isMutable,
        std::optional<std::string> lifetime
    )
        : isReference_(isReference),
          isMutable_(isMutable),
          lifetime_(std::move(lifetime)) {}

    void dump(std::ostream &out, int indent = 0) const override;
};

class FunctionItem final : public Item{
private:
    std::string name_;
    std::unique_ptr<SelfFunctionParam> selfParam_;   // nullptr 表示没有 self 参数
    std::vector<std::unique_ptr<FunctionParam>> parameters_;  // 可以为空，表示没有普通参数
    std::unique_ptr<TypeRef> returnType_;  // nullptr 表示没有写 -> 类型
    std::unique_ptr<WhereClause> whereClause_;  // nullptr 表示没有 where 子句。
    std::unique_ptr<BlockExpr> body_;
    std::unique_ptr<GenericParams> genericParams_;
public:
    FunctionItem(
        std::string name,
        std::unique_ptr<SelfFunctionParam> selfParam,
        std::vector<std::unique_ptr<FunctionParam>> parameters,
        std::unique_ptr<TypeRef> returnType,
        std::unique_ptr<BlockExpr> body,
        std::unique_ptr<WhereClause> whereClause = nullptr,
        std::unique_ptr<GenericParams> genericParams = nullptr
    ): name_(std::move(name)), selfParam_(std::move(selfParam)), parameters_(std::move(parameters)), returnType_(std::move(returnType)), whereClause_(std::move(whereClause)), body_(std::move(body)), genericParams_(std::move(genericParams)) {}

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

    void dump(std::ostream &out, int indent = 0) const override;
};

// 字段类型复用 TypeRef；字段重名和类型是否合法留给语义分析。
class StructField final : public ASTNode {
    std::string name_;
    std::unique_ptr<TypeRef> type_;

public:
    StructField(std::string name, std::unique_ptr<TypeRef> type)
        : name_(std::move(name)), type_(std::move(type)) {}

    void dump(std::ostream &out, int indent = 0) const override;
};

class OuterAttribute : public ASTNode {};

// #[derive(...)]：保留每个属性的边界，以及名称的顺序、重复和空列表。
class DeriveAttribute final : public OuterAttribute {
    std::vector<std::string> names_;

public:
    explicit DeriveAttribute(std::vector<std::string> names)
        : names_(std::move(names)) {}

    void dump(std::ostream &out, int indent = 0) const override;
};

// struct NAME { field: Type, ... }：按源码顺序保存字段，也允许空字段列表。
class StructItem final : public Item {
    std::string name_;
    std::vector<std::unique_ptr<StructField>> fields_;
    std::unique_ptr<WhereClause> whereClause_;
    std::vector<std::unique_ptr<OuterAttribute>> attributes_;
    std::unique_ptr<GenericParams> genericParams_;

public:
    StructItem(std::string name, std::vector<std::unique_ptr<StructField>> fields,
               std::unique_ptr<WhereClause> whereClause = nullptr,
               std::vector<std::unique_ptr<OuterAttribute>> attributes = {},
               std::unique_ptr<GenericParams> genericParams = nullptr)
        : name_(std::move(name)), fields_(std::move(fields)), whereClause_(std::move(whereClause)),
          attributes_(std::move(attributes)), genericParams_(std::move(genericParams)) {}

    void dump(std::ostream &out, int indent = 0) const override;
};

// impl 是独立的声明节点，按源码顺序保存方法和关联常量。
class ImplItem final : public Item {
    std::unique_ptr<TypeRef> type_;
    std::vector<ItemPtr> items_;
    std::unique_ptr<WhereClause> whereClause_;
    std::unique_ptr<GenericParams> genericParams_;

public:
    ImplItem(std::unique_ptr<TypeRef> type, std::vector<ItemPtr> items,
             std::unique_ptr<WhereClause> whereClause,
             std::unique_ptr<GenericParams> genericParams)
        : type_(std::move(type)), items_(std::move(items)), whereClause_(std::move(whereClause)),
          genericParams_(std::move(genericParams)) {}

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

    void dump(std::ostream &out, int indent = 0) const override;
};

class ExprStmt final : public Stmt {
    ExprPtr expression_;

public:
    explicit ExprStmt(ExprPtr expression): expression_(std::move(expression)){};

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

    void dump(std::ostream &out, int indent = 0) const override;
};

class BooleanLiteralExpr final : public Expr{
    bool flag_;

public:
    BooleanLiteralExpr(bool flag): flag_(flag){};

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

    void dump(std::ostream &out, int indent = 0) const override;
};

class UnaryExpr final : public Expr{
    std::string op_;  // -、!、*、& 或 &mut；连续借用保存为嵌套节点。
    ExprPtr operand_;

public:
    UnaryExpr(std::string op, ExprPtr operand): op_(std::move(op)), operand_(std::move(operand)){};

    void dump(std::ostream &out, int indent = 0) const override;
};

class CastExpr final : public Expr {
    ExprPtr value_;
    std::unique_ptr<TypeRef> type_;  // as 的右侧是类型，不是值表达式。

public:
    CastExpr(ExprPtr value, std::unique_ptr<TypeRef> type)
        : value_(std::move(value)), type_(std::move(type)) {}

    void dump(std::ostream &out, int indent = 0) const override;
};

class GenericArgument : public ASTNode {}; //泛型参数的公共基类

class TypeGenericArgument final : public GenericArgument { //持有现有的 `TypeRef`，复用数组、引用等类型构建逻辑
    std::unique_ptr<TypeRef> type_;

public:
    explicit TypeGenericArgument(std::unique_ptr<TypeRef> type)
        : type_(std::move(type)) {}

    void dump(std::ostream &out, int indent = 0) const override;
};

class LifetimeGenericArgument final : public GenericArgument {
    std::string lifetime_;

public:
    explicit LifetimeGenericArgument(std::string lifetime)
        : lifetime_(std::move(lifetime)) {}

    void dump(std::ostream &out, int indent = 0) const override;
};

using GenericArgs = std::vector<std::unique_ptr<GenericArgument>>;

// eg.Container::<'a, i32>::new::<u32>()
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

    void dump(std::ostream &out, int indent = 0) const override;
};

class PathExpr final : public Expr{
    std::vector<std::unique_ptr<PathSegment>> segments_;

public:
    explicit PathExpr(std::vector<std::unique_ptr<PathSegment>> segments)
        : segments_(std::move(segments)) {}

    void dump(std::ostream &out, int indent = 0) const override;
};

class StructExprField final : public ASTNode {
    std::string name_;
    ExprPtr value_;

public:
    StructExprField(std::string name, ExprPtr value)
        : name_(std::move(name)), value_(std::move(value)) {}

    void dump(std::ostream &out, int indent = 0) const override;
};

// 路径保留泛型实参；初始化字段按源码顺序保存，供后续确定求值顺序。
class StructExpr final : public Expr {
    std::unique_ptr<PathExpr> path_;
    std::vector<std::unique_ptr<StructExprField>> fields_;

public:
    StructExpr(std::unique_ptr<PathExpr> path, std::vector<std::unique_ptr<StructExprField>> fields)
        : path_(std::move(path)), fields_(std::move(fields)) {}

    void dump(std::ostream &out, int indent = 0) const override;
};

class FieldExpr final : public Expr {
    ExprPtr base_;
    std::string field_;

public:
    FieldExpr(ExprPtr base, std::string field): base_(std::move(base)), field_(std::move(field)) {}

    void dump(std::ostream &out, int indent = 0) const override;
};

class MethodCallExpr final : public Expr {
    ExprPtr receiver_;
    std::unique_ptr<PathSegment> method_;
    std::vector<ExprPtr> arguments_;

public:
    MethodCallExpr(ExprPtr receiver, std::unique_ptr<PathSegment> method, std::vector<ExprPtr> arguments)
        : receiver_(std::move(receiver)), method_(std::move(method)), arguments_(std::move(arguments)) {}

    void dump(std::ostream &out, int indent = 0) const override;
};

class AssignExpr final : public Expr {
private:
    ExprPtr target_;
    ExprPtr value_;

public:
    AssignExpr(ExprPtr target, ExprPtr value): target_(std::move(target)), value_(std::move(value)) {}

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

    void dump(std::ostream &out, int indent = 0) const override;
};

class IfExpr final : public Expr {
private:
    ExprPtr condition_;
    std::unique_ptr<BlockExpr> thenBranch_;
    ExprPtr elseBranch_;

public:
    IfExpr(ExprPtr condition, std::unique_ptr<BlockExpr> thenBranch, ExprPtr elseBranch): condition_(std::move(condition)), thenBranch_(std::move(thenBranch)), elseBranch_(std::move(elseBranch)) {}

    void dump(std::ostream &out, int indent = 0) const override;
};

class CallExpr final : public Expr{
    // 被调用者可以是任意表达式，例如 f()(x) 或 a[i](x) 中前半部分。
    ExprPtr callee_;
    std::vector<ExprPtr> arguments_;  // 按源码顺序保存；f() 的参数列表为空。

public:
    CallExpr(ExprPtr callee, std::vector<ExprPtr> arguments)
        : callee_(std::move(callee)), arguments_(std::move(arguments)) {}

    void dump(std::ostream &out, int indent = 0) const override;
};

class ReturnExpr final : public Expr{  //occur twice in parser.g4 nonblockPrimary & conditionPrimaryWithoutBareBlock 
private:
    ExprPtr value_;   //nullptr : return;
public:
    explicit ReturnExpr(ExprPtr value): value_(std::move(value)){};

    void dump(std::ostream &out, int indent = 0) const override;
};

// 与 return 一样，break 和 continue 都作为表达式节点构建。
// 循环归属和 break 值的合法性留给语义分析判断。
class BreakExpr final : public Expr{
private:
    ExprPtr value_;   // nullptr 表示 break;，否则持有 break 后面的值。
public:
    explicit BreakExpr(ExprPtr value): value_(std::move(value)){};

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

    void dump(std::ostream &out, int indent = 0) const override;
};

// while condition { ... }：节点分别持有条件表达式和循环体。
class WhileExpr final : public Expr{
private:
    ExprPtr condition_;
    std::unique_ptr<BlockExpr> block_;
public:
    WhileExpr(ExprPtr condition, std::unique_ptr<BlockExpr> block): condition_(std::move(condition)), block_(std::move(block)){};

    void dump(std::ostream &out, int indent = 0) const override;
};


class ArrayExpr final : public Expr{
private:
    std::vector<ExprPtr> elements_;

public:
    ArrayExpr(std::vector<ExprPtr> elements) : elements_(std::move(elements)){};

    void dump(std::ostream &out, int indent = 0) const override;
};

class ArrayRepeatExpr final : public Expr{
private:
    ExprPtr value_;
    ExprPtr count_;   // 保存次数的常量表达式，后续语义分析再求值
public:
    ArrayRepeatExpr(ExprPtr value, ExprPtr count) : value_(std::move(value)), count_(std::move(count)){};

    void dump(std::ostream &out, int indent = 0) const override;
};

class IndexExpr final : public Expr{
private:
    ExprPtr base_;
    ExprPtr index_;
public:
    IndexExpr(ExprPtr base, ExprPtr index) : base_(std::move(base)), index_(std::move(index)){};

    void dump(std::ostream &out, int indent = 0) const override;
};
}
