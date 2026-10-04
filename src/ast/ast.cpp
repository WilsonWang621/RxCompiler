#include "ast.hpp"

namespace{
    void printIndent(std::ostream &out, int indent){
        for(int i = 0; i < indent; i++){
            out << " ";
        }
    }
}
namespace rx::ast{
    void BlockExpr::dump(std::ostream &out, int indent) const {
        printIndent(out, indent);
        out << "Block\n";
        for (const auto &statement : stmts_) {
            statement->dump(out, indent + 1);
        }
        if(tail_ != nullptr){
            tail_->dump(out, indent + 1);
        }
    }

    void FunctionItem::dump(std::ostream &out, int indent) const {
        printIndent(out, indent);
        out << "Function: " << name_ << '\n';

        if (selfParam_ != nullptr) {
            selfParam_->dump(out, indent + 1);
        }

        for (const auto &parameter : parameters_) {
            parameter->dump(out, indent + 1);
        }

        if (returnType_ != nullptr) {
            printIndent(out, indent + 1);
            out << "ReturnType\n";
            returnType_->dump(out, indent + 2);
        }
        body_->dump(out, indent + 1);
    }

    void ConstItem::dump(std::ostream &out, int indent) const {
        printIndent(out, indent);
        out << "ConstItem: " << name_ << '\n';

        printIndent(out, indent + 1);
        out << "Type:\n";
        type_->dump(out, indent + 2);

        printIndent(out, indent + 1);
        out << "Value:\n";
        value_->dump(out, indent + 2);
    }

    void Crate::dump(std::ostream &out, int indent) const{
        printIndent(out, indent);
        out << "Crate\n";
        for(const auto &item : items){
            item->dump(out, indent + 1);
        }
    }

    void LetStmt::dump(std::ostream &out, int indent) const{
        printIndent(out, indent);

        out << "LetStmt: ";
        if (isMutable_) {
            out << "mut ";
        }
        out << name_ << '\n';

        initializer_->dump(out, indent + 1);
    }

    void IntegerLiteralExpr::dump(std::ostream &out, int indent) const{
        printIndent(out, indent);
        out << "IntegerLiteral: " << text_ << '\n';
    }

    void BooleanLiteralExpr::dump(std::ostream &out, int indent) const{
        printIndent(out, indent);
        out << "Boolean: " << (flag_ ? "true" : "false") << '\n';
    }

    void BinaryExpr::dump(std::ostream &out, int indent) const {
        printIndent(out, indent);
        out << "BinaryExpr: " << op_ << '\n';

        left_->dump(out, indent + 1);
        right_->dump(out, indent + 1);
    }

    void UnaryExpr::dump(std::ostream &out, int indent) const{
        printIndent(out, indent);
        out << "UnaryExpr: " << op_ << '\n';

        operand_->dump(out, indent + 1);
    }

    void PathExpr::dump(std::ostream &out, int indent) const{
        printIndent(out, indent);
        out << "PathExpr: ";

        for (std::size_t i = 0; i < segments_.size(); ++i) {
            if (i != 0) {
                out << "::";
            }

            out << segments_[i];
        }

        out << '\n'; 
    }

    void ExprStmt::dump(std::ostream &out, int indent) const{
        printIndent(out, indent);
        out << "ExprStmt:\n";
        expression_->dump(out, indent + 1);
    }

    void AssignExpr::dump(std::ostream &out, int indent) const {
        printIndent(out, indent);
        out << "AssignExpr\n";

        // 第一个孩子是赋值目标，第二个孩子是右侧的值。
        target_->dump(out, indent + 1);
        value_->dump(out, indent + 1);
    }

    void IfExpr::dump(std::ostream &out, int indent) const{
        printIndent(out, indent);
        out << "IfExpr\n";

        printIndent(out, indent + 1);
        out << "Condition:\n";
        condition_->dump(out, indent + 2);

        printIndent(out, indent + 1);
        out << "Then:\n";
        thenBranch_->dump(out, indent + 2);

        printIndent(out, indent + 1);
        if(elseBranch_ != nullptr){
            out << "Else:\n";
            elseBranch_->dump(out, indent + 2);
        }else{
            out << "Else: <none>\n";
        } 
    }

    void LoopExpr::dump(std::ostream &out, int indent) const{
        printIndent(out, indent);
        out << "LoopExpr\n";

        printIndent(out, indent + 1);
        out << "Body:\n";
        block_->dump(out, indent + 2);
    }

    // Condition 和 Body 是同级分组，各自的子树再缩进一层。
    void WhileExpr::dump(std::ostream &out, int indent) const{
        printIndent(out, indent);
        out << "WhileExpr\n";

        printIndent(out, indent + 1);
        out << "Condition:\n";
        condition_->dump(out, indent + 2);

        printIndent(out, indent + 1);
        out << "Body:\n";
        block_->dump(out, indent + 2);
    }

    void TypeRef::dump(std::ostream &out, int indent) const{
        printIndent(out, indent);
        out << "TypeRef: " << type_ << '\n';
    }

    void NamedFunctionParam::dump(std::ostream &out, int indent) const{
        printIndent(out, indent);
        out << "Parameter: ";
        if (isMutable_) {
            out << "mut ";
        }
        out << name_ << '\n';
        type_->dump(out, indent + 1);
    }

    void SelfFunctionParam::dump(std::ostream &out, int indent) const{
        printIndent(out, indent);
        out << "Parameter: ";
        if (isReference_) {
            out << '&';
            if (lifetime_) {
                out << *lifetime_ << ' ';
            }
        }
        if (isMutable_) {
            out << "mut ";
        }
        out << "self\n";
    }

    void CallExpr::dump(std::ostream &out, int indent) const{
        printIndent(out, indent);
        
    }

    void BreakExpr::dump(std::ostream &out, int indent) const{
        printIndent(out, indent);
        out << "BreakExpr\n";

        if(value_ != nullptr){
            value_->dump(out, indent + 1);
        }else{
            printIndent(out, indent + 1);
            out << "<no value>\n";
        }
    }

    void ContinueExpr::dump(std::ostream &out, int indent) const{
        printIndent(out, indent);
        out << "ContinueExpr\n";
    }

    void ReturnExpr::dump(std::ostream &out, int indent) const {
        printIndent(out, indent);
        out << "ReturnExpr\n";

        if (value_ != nullptr) {
            value_->dump(out, indent + 1);
        } else {
            printIndent(out, indent + 1);
            out << "<no value>\n";
        }
    }

    void ArrayExpr::dump(std::ostream &out, int indent) const{
        printIndent(out, indent);
        out << "ArrayExpr\n";

        for(const auto &element : elements_){
            element->dump(out, indent + 1);
        }
    }

    void ArrayRepeatExpr::dump(std::ostream &out, int indent) const{
        printIndent(out, indent);
        out << "ArrayRepeatExpr\n";

        printIndent(out, indent + 1);
        out << "Value:\n";
        value_->dump(out, indent + 2);

        printIndent(out, indent + 1);
        out << "Count:\n";
        count_->dump(out, indent + 2);
    }

    void IndexExpr::dump(std::ostream &out, int indent) const{
        printIndent(out, indent);
        out << "IndexExpr\n";

        printIndent(out, indent + 1);
        out << "Base:\n";
        base_->dump(out, indent + 2);

        printIndent(out, indent + 1);
        out << "Index:\n";
        index_->dump(out, indent + 2);
    }
}
