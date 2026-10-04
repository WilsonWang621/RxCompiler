# 设计改进

本页解释已经采用的设计及其原因。相关源码集中在 [ast.hpp](../src/ast/ast.hpp) 和 [ast_builder.cpp](../src/frontend/ast_builder.cpp)。

## 1. 从节点草图收缩到可运行的小闭环

早期 `155b0ab` 加入了较多节点、源码位置等设计草图，`1d9f133` 随后收缩为较小的 AST。之后逐步接通空函数、块、字面量和表达式构建。

这次取舍让每个新增结构都能通过 AST 输出观察，不必等待所有节点完成才开始验证。代价是源码位置等能力仍需要以后补齐，不能把草图中的结构算作已实现功能。

## 2. AST 子节点使用 `unique_ptr` 表达所有权

`ExprPtr` 是 `std::unique_ptr<ast::Expr>`。数组元素、下标、调用参数及各种操作数都用它持有子树。

构建函数可以实际创建 `IntegerLiteralExpr`、`PathExpr`、`UnaryExpr` 等不同节点，并通过统一的 `ExprPtr` 返回。构造父节点时用 `std::move` 转移所有权，AST 不依赖解析树中 Context 对象的存活时间。

整数原文、标识符等必要文本会复制进节点。Parser Context 描述语法，AST 节点保存后续阶段需要的程序结构。

## 3. 常量保留表达式，整数保留原文

`buildConstValue()` 构建常量表达式，返回 `ExprPtr`。`N` 是 `PathExpr`，`-N` 是 `UnaryExpr` 包住 `PathExpr`，此时不查询符号表，也不求出数值。

整数构建曾有“只支持无后缀十进制”的阶段性限制。后来由 `994d252` 扩展为保存完整字面量原文，并由公共 `buildIntegerLiteral()` 统一处理各入口：

```text
0xDeAd_BeEfusize → IntegerLiteralExpr("0xDeAd_BeEfusize")
```

Lexer 检查字面量的词法格式；数值解析、类型后缀解释及范围检查留给语义分析。这使普通字面量、数组长度、负号后的整数拥有一致的支持范围，也避免 AST 构建被机器整数的范围限制。

## 4. 数组值、数组类型和下标分别建模

| 源码 | 节点 | 保存的内容 |
| --- | --- | --- |
| `[a, b]` | `ArrayExpr` | 按顺序保存元素表达式 |
| `[a; N]` | `ArrayRepeatExpr` | 一个元素值表达式和次数表达式 |
| `[i32; N]`，位于类型位置 | `ArrayTypeRef` | 元素类型和长度表达式 |
| `a[i]` | `IndexExpr` | 被索引的表达式和下标表达式 |

重复数组在 AST 阶段保留 `value` 与 `count`，不会展开成 N 个元素。这样既支持尚未解析的常量名称，也保留后续求值需要的结构。

多维结构通过递归构建自然形成：`buildArray()` 把元素交给 `buildExpression()`，内层数组会再次进入 `buildArray()`；`buildArrayType()` 把元素类型交给 `buildTypeRef()`，内层类型也可以是数组。

相关提交：`fcda6f4`、`6f74a30`、`2089039`。

## 5. 类型采用共同基类，变量注解保持可选

类型从单一文本表示扩展为 `TypeRef` 基类：

- `SimpleTypeRef` 保存类型路径或 `()`。
- `ArrayTypeRef` 保存元素类型和长度。
- `ReferenceTypeRef` 保存被引用类型、可变性和可选生命周期。

函数参数、返回值、常量声明、变量注解及 `as` 目标类型统一持有 `std::unique_ptr<TypeRef>`。

`LetStmt` 的注解指针允许为空，表示源码没有写类型。Builder 同时保存注解和初始化值，二者是否匹配，以及无注解时如何推导类型，都留给语义分析。

类型路径目前仍保存为文本，尚未拆成完整的语义类型或执行名称解析。

## 6. 保留不同表达式入口，共用构建操作

普通表达式、语句起始表达式、条件表达式及条件中的 `break` 操作数有不同的语法约束。例如条件中的裸 `break` 不能把紧随其后的循环体当作值。

因此各入口仍由对应 Context 的构建函数适配；公共的组树操作抽成辅助函数：

| 辅助函数 | 作用 |
| --- | --- |
| `buildBinaryChain()` | 同层二元运算从左到右折叠 |
| `buildOptionalBinary()` | 构建可选的单次比较 |
| `buildAssignment()` | 保留赋值的右侧递归 |
| `buildPostfixSuffix()` | 把调用或下标包装到此前的表达式上 |
| `buildCastChain()` | 按顺序包装连续类型转换 |
| `buildUnaryOperator()` | 统一处理一元运算，展开双重借用 |

新增功能时，需要检查各入口是否都能到达公共构建逻辑。只修改普通表达式入口，会漏掉语句、尾表达式或条件中的同一种操作。

## 7. 后缀按顺序包装，调用者也是表达式

各 postfix 入口先构建主表达式，再依次处理后缀。`base` 始终是上一步得到的整棵子树。

例如 `f()[i](x)` 的构建顺序：

```text
PathExpr(f)
→ CallExpr(callee=f, arguments=[])
→ IndexExpr(base=上一棵树, index=i)
→ CallExpr(callee=上一棵树, arguments=[x])
```

所以 `CallExpr::callee_` 使用 `ExprPtr`，能够表示链式调用、下标后调用及括号表达式作为调用者。是否真的可调用、参数数量和类型是否正确，需要后续检查。

`f()` 的参数向量为空，调用括号本身不会生成一个 `()` 表达式。即使调用位于条件中，参数仍按 `callArguments` 中的普通 `expression` 规则递归构建。

相关提交：`fce1425`、`0ce058d`。

## 8. 移位按解析树顺序折叠

移位规则同时使用普通 additive 和 closed additive 操作数。ANTLR 给出的两个 Context 向量各自保持顺序，但分开遍历后会丢失两类操作数的交错关系。

`buildShiftChain()` 按规则的孩子顺序处理“操作数、运算符、操作数”，再由 `buildShiftOperand()` 分派不同入口的操作数。`shiftRight` 虽由两个 token 组成，它的 `getText()` 仍得到 `>>`。

因此 `a << b >> c` 构建成 `(a << b) >> c`。位运算等普通链条继续复用 `buildBinaryChain()`。

## 9. 复合赋值保持独立节点

`CompoundAssignExpr` 保存原运算符、目标和右侧值。例如：

```rust
values()[next()] += f()
```

如果提前改写为 `values()[next()] = values()[next()] + f()`，就可能重复执行目标中的函数调用。独立节点让后续阶段有机会按正确的求值规则降低复合赋值。

这只是 AST 表示；目标是否可以赋值，以及后续如何求值，还没有实现。

## 10. `as` 的右侧保存类型，双重借用保存两层结构

`CastExpr` 持有值表达式和 `TypeRef`。连续 `x as i32 as usize` 按左结合构建两层转换，优先级仍由语法层级决定。

closed 类型入口保留 `x as (i32) < y`、`x as (i32) << y` 的类型与运算边界。`closed` 表示消歧约束，不表示编译期常量。

`&&mut x` 构建为 `UnaryExpr("&", UnaryExpr("&mut", x))`，`mut` 属于内层借用。类型中的 `&&'a mut i32` 同样构建两层 `ReferenceTypeRef`，生命周期和 `mut` 属于内层。

相关提交：`5293204`、`4def470`。

## 11. 提交按模块保留可审查的边界

AST 节点、Builder、语法修正与测试分别提交。源码不依赖本地测试文件；测试移动到本地后，也保留了此前的测试提交以便查询和恢复。

修改支持范围时，同步更新旧的拒绝测试。修改语法时，从 `.g4` 重新生成解析器，而不是只手改生成文件。
