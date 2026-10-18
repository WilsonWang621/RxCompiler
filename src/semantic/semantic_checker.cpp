#include "semantic_checker.hpp"

#include <algorithm>
#include <limits>
#include <stdexcept>
#include <utility>

SemanticModel SemanticChecker::check(const rx::ast::Crate &crate){
    model_ = SemanticModel{};
    scope_ = Scope{};
    builtinSymbols_.clear();
    nextSymbolId_ = 1;

    firstScan(crate);
    secondScan(crate);

    return std::move(model_);
}

SymbolId SemanticChecker::allocateSymbol(){
    if(nextSymbolId_ == 0 || nextSymbolId_ == std::numeric_limits<std::uint32_t>::max()){
        throw std::overflow_error("too many semantic symbols");
    }
    return SymbolId{nextSymbolId_++};
}

void SemanticChecker::diagnose(const rx::ast::ASTNode &node, std::string message){
    model_.diagnostics.push_back(Diagnosis{node.sourceSpan(), std::move(message)});
}

void SemanticChecker::firstScan(const rx::ast::Crate &crate){
    // 先建立完整的顶层命名空间，允许前向引用。
    for(const auto &item : crate.items()){
        if(const auto *function = dynamic_cast<const rx::ast::FunctionItem *>(item.get())){
            collectFunction(*function);
        }else if(const auto *constant = dynamic_cast<const rx::ast::ConstItem *>(item.get())){
            collectConstant(*constant);
        }else if(const auto *structure = dynamic_cast<const rx::ast::StructItem *>(item.get())){
            collectStruct(*structure);
        }
    }

    // impl 依赖完整的类型命名空间，因此在普通顶层声明之后收集。
    for(const auto &item : crate.items()){
        if(const auto *implementation = dynamic_cast<const rx::ast::ImplItem *>(item.get())){
            collectImpl(*implementation);
        }
    }
}

void SemanticChecker::secondScan(const rx::ast::Crate &crate){
    // 建立全局作用域，依次解析声明类型、求值常量、检查结构体和函数体，最后校验 main。
    prepareGlobalScope();
}

void SemanticChecker::collectFunction(const rx::ast::FunctionItem &function){
    const SymbolId symbol = allocateSymbol();
    const bool inserted = model_.declarations.globalValues.emplace(function.name(), symbol).second;
    if(!inserted){
        diagnose(function, "duplicate value declaration '" + function.name() + "'");
        return;
    }

    model_.symbols.emplace(symbol, SymbolInfo{function.name(), SymbolKind::Function, function.id(), function.sourceSpan(), false});
    model_.declaredSymbols.emplace(function.id(), symbol);

    FunctionDeclarationInfo declaration{symbol, &function};
    for(std::size_t index = 0; index < function.parameters().size(); ++index){
        const auto *parameter = dynamic_cast<const rx::ast::NamedFunctionParam *>(function.parameters()[index].get());
        if(parameter == nullptr) continue;

        if(!declaration.parameters.emplace(parameter->name(), index).second){
            diagnose(*parameter, "duplicate parameter '" + parameter->name() + "'");
        }
    }
    model_.declarations.functions.emplace(symbol, std::move(declaration));
}

void SemanticChecker::collectConstant(const rx::ast::ConstItem &constant){
    const SymbolId symbol = allocateSymbol();
    const bool inserted = model_.declarations.globalValues.emplace(constant.name(), symbol).second;
    if(!inserted){
        diagnose(constant, "duplicate value declaration '" + constant.name() + "'");
        return;
    }

    model_.symbols.emplace(symbol, SymbolInfo{constant.name(), SymbolKind::Constant, constant.id(), constant.sourceSpan(), false});
    model_.declaredSymbols.emplace(constant.id(), symbol);
    model_.declarations.constants.emplace(symbol, ConstantDeclarationInfo{symbol, &constant});
}

void SemanticChecker::collectStruct(const rx::ast::StructItem &structure){
    const SymbolId symbol = allocateSymbol();
    const bool inserted = model_.declarations.globalTypes.emplace(structure.name(), symbol).second;
    if(!inserted){
        diagnose(structure, "duplicate type declaration '" + structure.name() + "'");
        return;
    }

    model_.symbols.emplace(symbol, SymbolInfo{structure.name(), SymbolKind::Struct, structure.id(), structure.sourceSpan(), false});
    model_.declaredSymbols.emplace(structure.id(), symbol);

    StructDeclarationInfo declaration{symbol, &structure};
    for(const auto &field : structure.fields()){
        if(!declaration.fields.emplace(field->name(), field.get()).second){
            diagnose(*field, "duplicate field '" + field->name() + "' in struct '" + structure.name() + "'");
        }
    }
    model_.declarations.structs.emplace(symbol, std::move(declaration));
}

void SemanticChecker::collectImpl(const rx::ast::ImplItem &implementation){
    const auto *target = dynamic_cast<const rx::ast::TypePathRef *>(&implementation.type());
    if(target == nullptr || target->segments().size() != 1 || target->segments().front()->hasGenericArgs()){
        diagnose(implementation, "impl target must be a non-generic user struct");
        return;
    }

    const std::string &typeName = target->segments().front()->name();
    const auto type = model_.declarations.globalTypes.find(typeName);
    if(type == model_.declarations.globalTypes.end()){
        diagnose(implementation, "unknown impl target '" + typeName + "'");
        return;
    }

    auto structure = model_.declarations.structs.find(type->second);
    if(structure == model_.declarations.structs.end()){
        diagnose(implementation, "impl target '" + typeName + "' is not a struct");
        return;
    }

    structure->second.implBlocks.push_back(&implementation);
    for(const auto &item : implementation.items()){
        const auto *function = dynamic_cast<const rx::ast::FunctionItem *>(item.get());
        const auto *constant = dynamic_cast<const rx::ast::ConstItem *>(item.get());
        if(function == nullptr && constant == nullptr){
            diagnose(*item, "unsupported item in impl block");
            continue;
        }

        const std::string &name = function != nullptr ? function->name() : constant->name();
        const SymbolId symbol = allocateSymbol();
        if(!structure->second.associatedValues.emplace(name, symbol).second){
            diagnose(*item, "duplicate associated value '" + name + "' for struct '" + typeName + "'");
            continue;
        }

        const SymbolKind kind = function != nullptr ? SymbolKind::Function : SymbolKind::Constant;
        model_.symbols.emplace(symbol, SymbolInfo{name, kind, item->id(), item->sourceSpan(), false});
        model_.declaredSymbols.emplace(item->id(), symbol);

        if(function != nullptr){
            FunctionDeclarationInfo declaration{symbol, function};
            for(std::size_t index = 0; index < function->parameters().size(); ++index){
                const auto *parameter = dynamic_cast<const rx::ast::NamedFunctionParam *>(function->parameters()[index].get());
                if(parameter == nullptr) continue;
                if(!declaration.parameters.emplace(parameter->name(), index).second){
                    diagnose(*parameter, "duplicate parameter '" + parameter->name() + "'");
                }
            }
            model_.declarations.functions.emplace(symbol, std::move(declaration));
        }else{
            model_.declarations.constants.emplace(symbol, ConstantDeclarationInfo{symbol, constant});
        }
    }
}

void SemanticChecker::prepareGlobalScope(){
    scope_ = Scope{};

    // 内建符号没有源码声明；重复准备作用域时复用符号，不重新分配 ID。
    auto declareBuiltin = [&](const std::string &name, bool isType){
        auto builtin = builtinSymbols_.find(name);
        if(builtin == builtinSymbols_.end()){
            const SymbolId symbol = allocateSymbol();
            model_.symbols.emplace(symbol, SymbolInfo{name, SymbolKind::Builtin, 0, {}, false});
            builtin = builtinSymbols_.emplace(name, symbol).first;
        }

        if(isType){
            scope_.declareType(name, builtin->second);
        }else{
            scope_.declareValue(name, builtin->second);
        }
    };

    for(const char *name : {"i32", "u32", "isize", "usize", "bool", "Box", "Vec",
                           "Copy", "Clone", "PartialEq", "Eq"}){
        declareBuiltin(name, true);
    }
    for(const char *name : {"get_i32", "print_i32", "println_i32"}){
        declareBuiltin(name, false);
    }

    // 全局表只包含顶层声明；关联项、字段和局部绑定不加入全局作用域。
    for(const auto &[name, symbol] : model_.declarations.globalValues){
        if(!scope_.declareValue(name, symbol)){
            const auto &info = model_.symbols.at(symbol);
            model_.diagnostics.push_back(Diagnosis{info.span, "cannot redeclare builtin value '" + name + "'"});
        }
    }
    for(const auto &[name, symbol] : model_.declarations.globalTypes){
        if(!scope_.declareType(name, symbol)){
            const auto &info = model_.symbols.at(symbol);
            model_.diagnostics.push_back(Diagnosis{info.span, "cannot redeclare builtin type '" + name + "'"});
        }
    }
}

void SemanticChecker::collectFunctionSignature(const rx::ast::FunctionItem &function, std::optional<TypeId> selfType){
    const auto symbol = model_.declaredSymbols.find(function.id());
    if(symbol == model_.declaredSymbols.end()) return; // 跳过第一遍已拒绝的重复声明。

    auto &declaration = model_.declarations.functions.at(symbol->second);
    declaration.parameterTypes.clear();
    declaration.parameterTypes.reserve(function.parameters().size());
    declaration.receiverType.reset();
    const TypeId errorType = model_.types.builtin(TypeKind::Error);

    for(const auto &parameter : function.parameters()){
        const auto *named = dynamic_cast<const rx::ast::NamedFunctionParam *>(parameter.get());
        if(named == nullptr){
            diagnose(*parameter, "expected an ordinary named parameter");
            declaration.parameterTypes.push_back(errorType);
        }else{
            declaration.parameterTypes.push_back(resolveType(named->type(), selfType));
        }
    }

    if(const auto *receiver = function.selfParam()){
        if(!selfType.has_value()){
            diagnose(*receiver, "self parameter is only allowed in an impl function");
            declaration.receiverType = errorType;
        }else{
            declaration.receiverType = receiver->isReference()
                ? model_.types.reference(*selfType, receiver->isMutable()) : *selfType;
        }
    }

    declaration.returnType = function.returnType() != nullptr
        ? resolveType(*function.returnType(), selfType) : model_.types.builtin(TypeKind::Unit);
}

void SemanticChecker::collectConstantType(const rx::ast::ConstItem &constant, std::optional<TypeId> selfType){
    const auto symbol = model_.declaredSymbols.find(constant.id());
    if(symbol == model_.declaredSymbols.end()) return;

    model_.declarations.constants.at(symbol->second).type = resolveType(constant.type(), selfType);
}

void SemanticChecker::collectStructField(const rx::ast::StructField &field, TypeId selfType){
    const auto &type = model_.types.get(selfType);
    if(type.kind() != TypeKind::Struct){
        diagnose(field, "field owner must be a struct");
        return;
    }

    auto &structure = model_.declarations.structs.at(type.symbol());
    const auto declaredField = structure.fields.find(field.name());
    // 重名字段不能覆盖第一遍保留的字段类型，也不能写入其他结构体的字段。
    if(declaredField == structure.fields.end() || declaredField->second != &field) return;

    structure.fieldTypes.insert_or_assign(field.name(), resolveType(field.type(), selfType));
}

void SemanticChecker::collectImplTypes(const rx::ast::ImplItem &implementation){
    // 第一遍已校验 impl 目标；这里复用声明身份，不重复报告非法目标。
    const auto *target = dynamic_cast<const rx::ast::TypePathRef *>(&implementation.type());
    if(target == nullptr || target->segments().size() != 1 || target->segments().front()->hasGenericArgs()) return;

    const auto symbol = model_.declarations.globalTypes.find(target->segments().front()->name());
    if(symbol == model_.declarations.globalTypes.end()) return;
    const auto structure = model_.declarations.structs.find(symbol->second);
    if(structure == model_.declarations.structs.end()) return;

    const auto &blocks = structure->second.implBlocks;
    if(std::find(blocks.begin(), blocks.end(), &implementation) == blocks.end()) return;

    const TypeId selfType = model_.types.structure(symbol->second);
    model_.resolvedTypes.insert_or_assign(implementation.type().id(), selfType);
    for(const auto &item : implementation.items()){
        if(const auto *function = dynamic_cast<const rx::ast::FunctionItem *>(item.get())){
            collectFunctionSignature(*function, selfType);
        }else if(const auto *constant = dynamic_cast<const rx::ast::ConstItem *>(item.get())){
            collectConstantType(*constant, selfType);
        }
    }
}

void SemanticChecker::collectFunctionBody(const rx::ast::FunctionItem &function, std::optional<TypeId> selfType){
    // 建立函数作用域和返回类型上下文，声明参数后检查函数体及所有 return 的类型。
}

void SemanticChecker::collectFunctionParameters(const rx::ast::FunctionItem &function, std::optional<TypeId> selfType){
    // 按源码顺序声明 self 和普通参数，为参数分配符号并加入当前函数作用域。
}

void SemanticChecker::collectParameter(const rx::ast::NamedFunctionParam &parameter){
    // 读取已解析的参数类型，创建带可变性信息的参数符号，并检查当前层重名。
}

void SemanticChecker::collectSelfParameter(const rx::ast::SelfFunctionParam &parameter, TypeId selfType){
    // 根据 self、mut self、&self 或 &mut self 构造接收者类型，并声明 self 符号。
}

void SemanticChecker::collectImplBodies(const rx::ast::ImplItem &implementation){
    // 在 impl 的 Self 类型上下文中检查所有方法、关联函数和关联常量初始化式。
}

void SemanticChecker::checkMain(const rx::ast::Crate &crate){
    // 检查唯一入口 main 的位置、参数、泛型和返回类型是否满足 Rx 程序入口要求。
}

TypeId SemanticChecker::resolveType(const rx::ast::TypeRef &type, std::optional<TypeId> selfType){
    // 分派 unit、路径、引用和数组类型解析，并把结果记录到 resolvedTypes。
}

TypeId SemanticChecker::resolveTypePath(const rx::ast::TypePathRef &type, std::optional<TypeId> selfType){
    // 解析内建类型、Self、用户结构体及 Box/Vec 实参，诊断路径和泛型实参数量错误。
}

std::optional<SymbolId> SemanticChecker::resolveValuePath(const rx::ast::PathExpr &path){
    // 按路径形态查找局部值、全局值或 Type::member，并记录最终解析到的符号。
}

std::optional<ConstValue> SemanticChecker::evaluateConstant(SymbolId symbol){
    // 按需计算常量并缓存结果，维护求值状态以发现常量之间的循环依赖。
}

std::optional<ConstValue> SemanticChecker::evaluateConstantExpr(const rx::ast::Expr &expression, TypeId expectedType){
    // 按 Rx 常量上下文允许的表达式递归求值，同时检查结果是否符合声明类型。
}

std::optional<std::uint32_t> SemanticChecker::evaluateArrayLength(const rx::ast::Expr &expression){
    // 以 usize 为预期类型求值数组长度，检查常量形式、范围以及禁止的零长度情况。
}

TypeId SemanticChecker::collectFunctionBlock(const rx::ast::BlockExpr &body, std::optional<TypeId> expectedType){
    // 为普通嵌套块建立 ScopeGuard，检查块内容并返回尾表达式或 unit 的类型。
}

TypeId SemanticChecker::collectBlockContents(const rx::ast::BlockExpr &body, std::optional<TypeId> expectedType){
    // 在已有作用域中按顺序检查语句，再按预期类型检查尾表达式并确定块类型。
}

TypeId SemanticChecker::collectStmt(const rx::ast::Stmt &stmt){
    // 分派空语句、let 和表达式语句，并保证带分号语句的结果类型为 unit。
}

void SemanticChecker::collectLetStmt(const rx::ast::LetStmt &stmt){
    // 先检查初始化式，再确定或验证绑定类型，最后创建局部符号并加入当前作用域。
}

TypeId SemanticChecker::collectExprStmt(const rx::ast::ExprStmt &stmt){
    // 检查内部表达式，并依据是否带分号返回表达式类型或 unit。
}

TypeId SemanticChecker::collectExpr(const rx::ast::Expr &expression, std::optional<TypeId> expectedType){
    // 按具体 AST 类型分派检查，记录 expressionTypes 和 place、可变性等 ExprInfo。
}

TypeId SemanticChecker::collectUnit(const rx::ast::UnitExpr &expression){
    // 将 unit 表达式标记为非 place，并返回内建 Unit 类型。
}

TypeId SemanticChecker::collectIntegerLiteral(const rx::ast::IntegerLiteralExpr &expression, std::optional<TypeId> expectedType){
    // 解析进制、下划线和后缀，按后缀、预期整数类型、i32 默认值的顺序确定类型。
}

TypeId SemanticChecker::collectBooleanLiteral(const rx::ast::BooleanLiteralExpr &expression){
    // 将布尔字面量标记为非 place，并返回内建 Bool 类型。
}

TypeId SemanticChecker::collectPath(const rx::ast::PathExpr &path){
    // 解析路径符号，取得常量或绑定的类型，并记录该表达式是否为可变 place。
}

TypeId SemanticChecker::collectBinaryExpr(const rx::ast::BinaryExpr &expression){
    // 按运算符检查左右操作数类型、短路规则和比较能力，确定运算结果类型。
}

TypeId SemanticChecker::collectUnaryExpr(const rx::ast::UnaryExpr &expression){
    // 检查负号、逻辑非、借用和解引用的操作数约束，并计算结果类型及 place 属性。
}

TypeId SemanticChecker::collectCast(const rx::ast::CastExpr &expression){
    // 解析目标类型，检查源类型是否允许显式 as 转换，并返回目标类型。
}

TypeId SemanticChecker::collectAssignment(const rx::ast::AssignExpr &assignment){
    // 检查左侧为可变 place，以其类型检查右侧的求值和隐式转换，结果为 unit。
}

TypeId SemanticChecker::collectCompoundAssignment(const rx::ast::CompoundAssignExpr &assignment){
    // 检查目标可变性及对应二元运算约束，保证目标只求值一次，结果为 unit。
}

TypeId SemanticChecker::collectArray(const rx::ast::ArrayExpr &array, std::optional<TypeId> expectedType){
    // 检查各元素，使用预期数组元素类型或 LUB 合并类型，并以元素数构造数组类型。
}

TypeId SemanticChecker::collectRepeatedArray(const rx::ast::ArrayRepeatExpr &repeatedArray, std::optional<TypeId> expectedType){
    // 检查重复值类型、常量长度及复制要求，并构造对应的固定数组类型。
}

TypeId SemanticChecker::collectIndex(const rx::ast::IndexExpr &expression){
    // 检查基值可索引且索引为 usize，返回元素类型并继承适当的 place 和可变性。
}

TypeId SemanticChecker::collectStructExpr(const rx::ast::StructExpr &expression){
    // 解析结构体，检查字段无重复、无遗漏且初始化值匹配声明类型，返回结构体类型。
}

TypeId SemanticChecker::collectField(const rx::ast::FieldExpr &expression){
    // 根据基表达式类型查找字段，返回字段类型并传播基 place 的可变访问能力。
}

TypeId SemanticChecker::collectCall(const rx::ast::CallExpr &expression){
    // 解析调用目标，按函数签名检查实参数量、求值顺序和类型，返回函数结果类型。
}

TypeId SemanticChecker::collectMethodCall(const rx::ast::MethodCallExpr &expression){
    // 根据接收者类型查找方法，执行允许的自动借用或解引用，并检查其余实参。
}

TypeId SemanticChecker::collectIf(const rx::ast::IfExpr &ifstmt, std::optional<TypeId> expectedType){
    // 要求条件为 bool，检查各分支，并按预期类型或 LUB 确定整个 if 的类型。
}

TypeId SemanticChecker::collectLoop(const rx::ast::LoopExpr &loop, std::optional<TypeId> expectedType){
    // 建立循环上下文，收集本层 break 值并确定循环结果类型；无正常出口时可为 never。
}

TypeId SemanticChecker::collectWhile(const rx::ast::WhileExpr &loop){
    // 要求条件为 bool，在循环上下文中检查 unit 循环体以及 break 和 continue。
}

TypeId SemanticChecker::collectReturn(const rx::ast::ReturnExpr &returnstmt){
    // 以当前函数返回类型检查返回值或 bare return，并将表达式类型记为 never。
}

TypeId SemanticChecker::collectBreak(const rx::ast::BreakExpr &breakstmt){
    // 确认位于循环内，把值加入最近循环的结果候选，并将表达式类型记为 never。
}

TypeId SemanticChecker::collectContinue(const rx::ast::ContinueExpr &continuestmt){
    // 确认位于循环内且无需结果值，并将表达式类型记为 never。
}

bool SemanticChecker::canCoerce(TypeId source, TypeId target) const{
    // 判断恒等、never、引用弱化以及规范允许的重复解引用再借用转换是否成立。
}

bool SemanticChecker::canCast(TypeId source, TypeId target) const{
    // 判断源和目标是否属于 Rx 允许的整数互转或 bool 到整数显式转换。
}

TypeId SemanticChecker::collectCommonType(const std::vector<TypeId> &types, const rx::ast::ASTNode &node){
    // 忽略 never 后按源码顺序执行 LUB 转换，失败时诊断并返回 Error 类型。
}

bool SemanticChecker::checkMutablePlace(const rx::ast::Expr &expression){
    // 查询表达式 place 信息，诊断不可赋值或不可变目标，并报告是否允许写入。
}

bool SemanticChecker::hasCopy(TypeId type) const{
    // 按内建类型、复合元素和结构体 derive 信息递归判断类型是否实现 Copy。
}

bool SemanticChecker::hasClone(TypeId type) const{
    // 按内建类型、容器元素和结构体 derive 信息递归判断类型是否实现 Clone。
}

bool SemanticChecker::hasPartialEq(TypeId type) const{
    // 按标量、复合元素和结构体 derive 信息递归判断类型是否支持相等比较。
}

bool SemanticChecker::hasEq(TypeId type) const{
    // 按标量、复合元素和结构体 derive 信息递归判断类型是否实现完整 Eq。
}

void SemanticChecker::checkStructLayout(const rx::ast::StructItem &structure){
    // 沿按值结构体和数组字段检测无限大小递归；引用、Box 和 Vec 截断递归路径。
}

void SemanticChecker::checkStructDerive(const rx::ast::StructItem &structure){
    // 校验 derive 名称、重复项和依赖关系，并确认所有字段满足派生 trait 的能力要求。
}
