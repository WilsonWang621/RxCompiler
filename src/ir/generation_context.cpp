#include "generation_context.hpp"

void IRGenerationContext::reset(IRModule &module, const SemanticModel &model){
    clear();
    builder.reset(&module);
    this->model = &model;
}

void IRGenerationContext::clear(){
    builder.reset();
    typeLowering.clear();
    locals.clear();
    constants.clear();
    functions.clear();
    structs.clear();
    loops = {};
    currentFunction = {};
    vectorStorage = nullptr;
    allocateFunction = nullptr;
    memcpyFunction = nullptr;
    memmoveFunction = nullptr;
    cloneFunctions.clear();
    equalityFunctions.clear();
    valueHelpers.clear();
    model = nullptr;
}
