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

//the top floor
class Crate : public ASTNode{
    std::vector<ItemPtr> items;

public:
    void addItem(ItemPtr item){
        items.push_back(std::move(item));
    }

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

class TypeRef final : public ASTNode{
private:
    std::string type_;
public:
    TypeRef(std::string type) : type_(std::move(type)){};

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
    std::unique_ptr<BlockExpr> body_;
public:
    FunctionItem(
        std::string name,
        std::unique_ptr<SelfFunctionParam> selfParam,
        std::vector<std::unique_ptr<FunctionParam>> parameters,
        std::unique_ptr<TypeRef> returnType,
        std::unique_ptr<BlockExpr> body
    ): name_(std::move(name)), selfParam_(std::move(selfParam)), parameters_(std::move(parameters)), returnType_(std::move(returnType)), body_(std::move(body)) {}

    void dump(std::ostream &out, int indent) const override;
};

//version 1.1
class LetStmt final : public Stmt{
    std::string name_;
    bool isMutable_;
    ExprPtr initializer_;

public:
    LetStmt(std::string name, bool isMutable, ExprPtr initializer):name_(std::move(name)), isMutable_(isMutable), initializer_(std::move(initializer)){};

    void dump(std::ostream &out, int indent = 0) const override;
};

class ExprStmt final : public Stmt {
    ExprPtr expression_;

public:
    explicit ExprStmt(ExprPtr expression): expression_(std::move(expression)){};

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
    std::string op_;
    ExprPtr operand_;

public:
    UnaryExpr(std::string op, ExprPtr operand): op_(std::move(op)), operand_(std::move(operand)){};

    void dump(std::ostream &out, int indent = 0) const override;
};

class PathExpr final : public Expr{
    std::vector<std::string> segments_;

public:
    explicit PathExpr(std::vector<std::string> segments) : segments_(std::move(segments)) {}

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
    ExprPtr callee_;
    std::vector<ExprPtr> arguments_;

public:
    CallExpr(ExprPtr callee, std::vector<ExprPtr> argument) : callee_(std::move(callee)), arguments_(std::move(argument)){};

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
