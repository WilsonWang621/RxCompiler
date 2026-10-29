#include "generation_context.hpp"

void IRGenerationContext::reset(IRModule &module, const SemanticModel &model){
    clear();
    builder.reset(&module);
    this->model = &model;
}

void IRGenerationContext::clear(){
    builder.reset();
    locals.clear();
    constants.clear();
    functions.clear();
    structs.clear();
    currentFunction = {};
    model = nullptr;
}
