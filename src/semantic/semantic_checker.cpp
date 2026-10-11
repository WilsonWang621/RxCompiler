#include "semantic_checker.hpp"

SemanticModel SemanticChecker::check(const rx::ast::Crate &crate){
    firstScan(crate);
    secondScan(crate);

    return std::move(model_);
}

void SemanticChecker::firstScan(const rx::ast::Crate &crate){
    
}

void SemanticChecker::collectFunction(const rx::ast::FunctionItem &function){

}