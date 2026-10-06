#include "ast.hpp"

namespace{
    void printIndent(std::ostream &out, int indent){
        for(int i = 0; i < indent; i++){
            out << " ";
        }
    }

    void dumpLifetimeBounds(std::ostream &out, int indent, const std::vector<std::string> &bounds) {
        printIndent(out, indent);
        out << "Bounds:\n";
        for (const auto &bound : bounds) {
            printIndent(out, indent + 1);
            out << "LifetimeBound: " << bound << '\n';
        }
    }
}
namespace rx::ast{
    void UsePath::dump(std::ostream &out, int indent) const {
        printIndent(out, indent);
        out << "UsePath: " << (isAbsolute_ ? "absolute" : "relative") << '\n';
        for (const auto &segment : segments_) {
            printIndent(out, indent + 1);
            out << "UsePathSegment: " << segment << '\n';
        }
    }

    void NamedUseTree::dump(std::ostream &out, int indent) const {
        printIndent(out, indent);
        out << "NamedUseTree\n";
        path_->dump(out, indent + 1);
        if (alias_) {
            printIndent(out, indent + 1);
            out << "Alias: " << *alias_ << '\n';
        }
    }

    void GlobUseTree::dump(std::ostream &out, int indent) const {
        printIndent(out, indent);
        out << "GlobUseTree\n";
        if (path_ != nullptr) {
            path_->dump(out, indent + 1);
        }
    }

    void GroupUseTree::dump(std::ostream &out, int indent) const {
        printIndent(out, indent);
        out << "GroupUseTree\n";
        if (path_ != nullptr) {
            path_->dump(out, indent + 1);
        }
        for (const auto &tree : trees_) {
            tree->dump(out, indent + 1);
        }
    }

    void UseItem::dump(std::ostream &out, int indent) const {
        printIndent(out, indent);
        out << "UseItem\n";
        tree_->dump(out, indent + 1);
    }

    void LifetimeParam::dump(std::ostream &out, int indent) const {
        printIndent(out, indent);
        out << "LifetimeParam: " << lifetime_ << '\n';
        if (bounds_) {
            dumpLifetimeBounds(out, indent + 1, *bounds_);
        }
    }

    void GenericParams::dump(std::ostream &out, int indent) const {
        printIndent(out, indent);
        out << "GenericParams\n";
        for (const auto &lifetime : lifetimes_) {
            lifetime->dump(out, indent + 1);
        }
    }

    void LifetimeWherePredicate::dump(std::ostream &out, int indent) const {
        printIndent(out, indent);
        out << "LifetimeWherePredicate: " << lifetime_ << '\n';
        dumpLifetimeBounds(out, indent + 1, bounds_);
    }

    void TypeWherePredicate::dump(std::ostream &out, int indent) const {
        printIndent(out, indent);
        out << "TypeWherePredicate\n";
        printIndent(out, indent + 1);
        out << "Type:\n";
        type_->dump(out, indent + 2);
        dumpLifetimeBounds(out, indent + 1, bounds_);
    }

    void WhereClause::dump(std::ostream &out, int indent) const {
        printIndent(out, indent);
        out << "WhereClause\n";
        for (const auto &predicate : predicates_) {
            predicate->dump(out, indent + 1);
        }
    }

    void EmptyStmt::dump(std::ostream &out, int indent) const {
        printIndent(out, indent);
        out << "EmptyStmt\n";
    }

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

        if (genericParams_ != nullptr) {
            genericParams_->dump(out, indent + 1);
        }

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
        if (whereClause_ != nullptr) {
            whereClause_->dump(out, indent + 1);
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

    void StructField::dump(std::ostream &out, int indent) const {
        printIndent(out, indent);
        out << "StructField: " << name_ << '\n';
        type_->dump(out, indent + 1);
    }

    void DeriveAttribute::dump(std::ostream &out, int indent) const {
        printIndent(out, indent);
        out << "DeriveAttribute\n";
        for (const auto &name : names_) {
            printIndent(out, indent + 1);
            out << "DeriveName: " << name << '\n';
        }
    }

    void StructItem::dump(std::ostream &out, int indent) const {
        printIndent(out, indent);
        out << "StructItem: " << name_ << '\n';
        for (const auto &attribute : attributes_) {
            attribute->dump(out, indent + 1);
        }
        if (genericParams_ != nullptr) {
            genericParams_->dump(out, indent + 1);
        }
        if (whereClause_ != nullptr) {
            whereClause_->dump(out, indent + 1);
        }
        for (const auto &field : fields_) {
            field->dump(out, indent + 1);
        }
    }

    void ImplItem::dump(std::ostream &out, int indent) const {
        printIndent(out, indent);
        out << "ImplItem\n";
        if (genericParams_ != nullptr) {
            genericParams_->dump(out, indent + 1);
        }
        printIndent(out, indent + 1);
        out << "Type:\n";
        type_->dump(out, indent + 2);
        if (whereClause_ != nullptr) {
            whereClause_->dump(out, indent + 1);
        }
        printIndent(out, indent + 1);
        out << "Items:\n";
        for (const auto &item : items_) {
            item->dump(out, indent + 2);
        }
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

        if (type_ != nullptr) {
            printIndent(out, indent + 1);
            out << "Type:\n";
            type_->dump(out, indent + 2);
        }

        initializer_->dump(out, indent + 1);
    }

    void IntegerLiteralExpr::dump(std::ostream &out, int indent) const{
        printIndent(out, indent);
        out << "IntegerLiteral: " << text_ << '\n';
    }

    void UnitExpr::dump(std::ostream &out, int indent) const {
        printIndent(out, indent);
        out << "UnitExpr\n";
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

    void CastExpr::dump(std::ostream &out, int indent) const {
        printIndent(out, indent);
        out << "CastExpr\n";

        printIndent(out, indent + 1);
        out << "Value:\n";
        value_->dump(out, indent + 2);

        printIndent(out, indent + 1);
        out << "Type:\n";
        type_->dump(out, indent + 2);
    }

    void TypeGenericArgument::dump(std::ostream &out, int indent) const {
        printIndent(out, indent);
        out << "TypeArgument\n";
        type_->dump(out, indent + 1);
    }

    void LifetimeGenericArgument::dump(std::ostream &out, int indent) const {
        printIndent(out, indent);
        out << "LifetimeArgument: " << lifetime_ << '\n';
    }

    void PathSegment::dump(std::ostream &out, int indent) const {
        printIndent(out, indent);
        out << "PathSegment: " << name_ << '\n';
        if (genericArgs_) {
            printIndent(out, indent + 1);
            out << "GenericArgs\n";
            for (const auto &argument : *genericArgs_) {
                argument->dump(out, indent + 2);
            }
        }
    }

    void PathExpr::dump(std::ostream &out, int indent) const{
        for (const auto &segment : segments_) {
            if (segment->hasGenericArgs()) {
                printIndent(out, indent);
                out << "PathExpr\n";
                for (const auto &pathSegment : segments_) {
                    pathSegment->dump(out, indent + 1);
                }
                return;
            }
        }

        printIndent(out, indent);
        out << "PathExpr: ";

        for (std::size_t i = 0; i < segments_.size(); ++i) {
            if (i != 0) {
                out << "::";
            }

            out << segments_[i]->name();
        }

        out << '\n'; 
    }

    void StructExprField::dump(std::ostream &out, int indent) const {
        printIndent(out, indent);
        out << "StructExprField: " << name_ << '\n';
        value_->dump(out, indent + 1);
    }

    void StructExpr::dump(std::ostream &out, int indent) const {
        printIndent(out, indent);
        out << "StructExpr\n";
        printIndent(out, indent + 1);
        out << "Path:\n";
        path_->dump(out, indent + 2);
        printIndent(out, indent + 1);
        out << "Fields:\n";
        for (const auto &field : fields_) {
            field->dump(out, indent + 2);
        }
    }

    void FieldExpr::dump(std::ostream &out, int indent) const {
        printIndent(out, indent);
        out << "FieldExpr: " << field_ << '\n';
        printIndent(out, indent + 1);
        out << "Base:\n";
        base_->dump(out, indent + 2);
    }

    void MethodCallExpr::dump(std::ostream &out, int indent) const {
        printIndent(out, indent);
        out << "MethodCallExpr\n";
        printIndent(out, indent + 1);
        out << "Receiver:\n";
        receiver_->dump(out, indent + 2);
        printIndent(out, indent + 1);
        out << "Method:\n";
        method_->dump(out, indent + 2);
        printIndent(out, indent + 1);
        out << "Arguments:\n";
        for (const auto &argument : arguments_) {
            argument->dump(out, indent + 2);
        }
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

    void CompoundAssignExpr::dump(std::ostream &out, int indent) const {
        printIndent(out, indent);
        out << "CompoundAssignExpr: " << op_ << '\n';
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

    void SimpleTypeRef::dump(std::ostream &out, int indent) const{
        printIndent(out, indent);
        out << "TypeRef: " << type_ << '\n';
    }

    void ReferenceTypeRef::dump(std::ostream &out, int indent) const {
        printIndent(out, indent);
        out << "ReferenceTypeRef: &";
        if (lifetime_) {
            out << *lifetime_;
        }
        if (isMutable_) {
            out << (lifetime_ ? " mut" : "mut");
        }
        out << '\n';
        referent_->dump(out, indent + 1);
    }

    void ArrayTypeRef::dump(std::ostream &out, int indent) const {
        printIndent(out, indent);
        out << "ArrayTypeRef\n";

        printIndent(out, indent + 1);
        out << "ElementType:\n";
        elementType_->dump(out, indent + 2);

        printIndent(out, indent + 1);
        out << "Count:\n";
        count_->dump(out, indent + 2);
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
        out << "CallExpr\n";

        printIndent(out, indent + 1);
        out << "Callee:\n";
        callee_->dump(out, indent + 2);

        printIndent(out, indent + 1);
        out << "Arguments:\n";
        for (const auto &argument : arguments_) {
            argument->dump(out, indent + 2);
        }
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
