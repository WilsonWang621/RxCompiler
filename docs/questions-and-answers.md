# 提问与答案

以下按主题整理实际提问及实现请求。相同提问合并，答案按当前代码更新；新增的设计解释写在答案中。

## 1. Array 中的 `ConstValueContext` 是分号前的值，还是分号后的数组大小？

在重复数组表达式 `[value; count]` 中，它对应分号后的 `count`。分号前的 `value` 是普通 `ExpressionContext`。

```cpp
auto value = buildExpression(ctx->expression(0));
auto count = buildConstValue(ctx->constValue());
```

在数组类型 `[i32; 3]` 中，分号前是 `TypeRefContext`，分号后仍是 `ConstValueContext`。列表数组 `[1, 2]` 没有这个次数 Context。

`constValue` 也用于 `const NAME: Type = value;` 的初始化值，并不只用于数组大小。

## 2. 高维数组怎么递归构造，应该在哪里实现？

数组值的递归放在 `buildArray()` 与 `buildExpression()` 之间；节点只负责保存构建好的子树。

```text
[[0; 3]; 2]
→ ArrayRepeatExpr
   value: ArrayRepeatExpr(value=0, count=3)
   count: 2
```

外层构建元素时调用 `buildExpression()`，它识别内层数组后再次调用 `buildArray()`。不需要按维数编写分支，也不需要在 AST 阶段展开元素。

数组类型的递归则由 `buildArrayType()` 调用 `buildTypeRef()` 处理，不能把类型交给表达式入口。

## 3. 为什么有 `buildMagnitude()`，它和 `buildConstValue()` 是什么关系？

它们对应不同的语法规则：

```antlr
constValue
    : INTEGER_LITERAL | TRUE | FALSE | pathInExpression
    | MINUS magnitude | LPAREN constValue RPAREN
    ;

magnitude
    : INTEGER_LITERAL | pathInExpression | LPAREN magnitude RPAREN
    ;
```

`buildConstValue()` 负责完整的常量表达式；遇到负号时，用 `buildMagnitude()` 构建负号后的内容，再包装一个 `UnaryExpr("-", operand)`。

这里的 magnitude 是“负号后的语法内容”，不是已经计算出来的绝对值。它可以是尚未解析的名称 `N`。

## 4. 如果不带负号呢？

直接在 `buildConstValue()` 中根据分支构建：

| 输入 | 返回的具体节点 |
| --- | --- |
| `3` | `IntegerLiteralExpr` |
| `N` | `PathExpr` |
| `true` / `false` | `BooleanLiteralExpr` |
| `(N)` | 递归构建括号里的 `constValue`，返回 `PathExpr` |

只有语法匹配 `MINUS magnitude` 时才调用 `buildMagnitude()`。纯分组括号不增加额外 AST 节点。

## 5. 返回什么类型？

`buildConstValue()` 和 `buildMagnitude()` 都返回 `ast::ExprPtr`：

```cpp
using ExprPtr = std::unique_ptr<Expr>;
```

具体对象可能是整数、路径或一元运算节点。各派生节点的 `unique_ptr` 可以移动转换为这个共同基类指针，父节点通过它持有子树。

因此返回类型不应固定为整数节点指针。

## 6. 如果是 `N`，它不是 `IntegerLiteralExpr`，怎么办？

构建 `PathExpr`，保留名称：

```cpp
return buildPath(ctx->pathInExpression());
```

AST 构建阶段不要求所有常量都变成整数。后续名称解析查找 `N`，常量求值计算它的值，再检查是否适合作为数组长度。

同理，`-N` 保存为负号节点包住路径节点。

## 7. 为什么这里不用 `buildLiteral()`，而是直接构建整数节点？

`buildLiteral()` 接收 `LiteralExpressionContext*`。当前 `ConstValueContext` 和 `MagnitudeContext` 的整数分支直接包含 `INTEGER_LITERAL` 终结符，没有一个 `literalExpression()` 子 Context 可以传进去。

所以早期可以直接写：

```cpp
return std::make_unique<ast::IntegerLiteralExpr>(
    ctx->INTEGER_LITERAL()->getText()
);
```

当前实现进一步抽出了 `buildIntegerLiteral(TerminalNode*)`：`buildLiteral()`、`buildConstValue()` 和 `buildMagnitude()` 都调用它，统一保存整数原文。

关键是共用底层节点构建逻辑，不是强行把不同 Context 当成同一种 Context。

## 8. `buildMagnitude()` 要递归构建吗？

遇到括号时需要递归，例如 `-((N))`：

```cpp
if (ctx->LPAREN() != nullptr) {
    return buildMagnitude(ctx->magnitude());
}
```

整数和路径是递归终点。外面的负号仍由 `buildConstValue()` 添加。

递归只处理这条规则允许的嵌套内容；它不构建数组，也不接受任意算术表达式。当前 `magnitude` 规则不允许再带负号或布尔值。

## 9. `postfixSuffix` 的三个分支分别对应 `IndexExpr` 的 value 和 index 哪一个？

三个分支表示三种不同的后缀操作，并不是 `IndexExpr` 的两个字段：

```antlr
postfixSuffix
    : callArguments
    | LBRACKET expression RBRACKET
    | dotSuffix
    ;
```

只有第二个分支构建 `IndexExpr`。以 `a[i]` 为例：

- `a` 是此前构建好的表达式，作为 `base` 传入。
- 方括号内的 `expression` 是 `i`，构建为 `index`。

当前成员名是 `base_` 和 `index_`；如果把被索引的表达式称为 value，它对应的就是这里的 base。

`callArguments` 构建 `CallExpr`，此前的 base 成为 callee；`dotSuffix` 对应字段或方法访问，目前尚未构建。

## 10. 数组类型 `[i32; 3]` 怎么实现？

由 `buildTypeRef()` 分派到 `buildArrayType()`，返回 `ArrayTypeRef`：

```text
ArrayTypeRef
 ElementType:
  TypeRef: i32
 Count:
  IntegerLiteral: 3
```

元素类型走 `buildTypeRef()`，长度走 `buildConstValue()`。`[[i32; 3]; 2]` 的元素类型也是 `ArrayTypeRef`，递归即可构建多维类型。

`LetStmt` 保存可选类型注解，因此 `let a: [i32; 3] = [0; 3];` 能同时保留声明类型和初始化值。

## 11. 数组检查后，补齐了哪些内容？

数组相关提交中可确认的内容包括：列表数组、重复数组、下标节点及 AST 输出；数组元素和下标使用表达式入口递归构建；普通、语句和条件等入口都能处理数组及下标后缀。

整数支持先统一为无后缀十进制，随后应“补上十六进制、下划线和类型后缀”的请求，由 `994d252` 扩展为 Lexer 已支持的整数格式，包括二进制和八进制。完整原文保存在 `IntegerLiteralExpr` 中，尚未做数值范围检查。

主要提交：`fcda6f4`、`994d252`。后续完整类型构建见 `6f74a30`、`2089039`。

## 12. 实现请求：构建 `constItem` 的 AST

`buildItem()` 识别 `constantItem`，交给 `buildConstItem()`，构建 `ConstItem(name, type, value)`。

类型由 `buildTypeRef()` 构建，初始化值由 `buildConstValue()` 构建。例如 `const N: usize = 3;` 保存名称、声明类型和整数字面量节点。

当前语法的初始化值仍受 `constValue` 规则限制；支持常量声明不意味着已经支持任意常量算术、依赖解析或求值。

主要提交：`36ce858`、`6fcbb73`。

## 13. 实现请求：完成函数调用 `CallExpr` 的构建

在共用的 `buildPostfixSuffix()` 中处理 `callArguments`，逐个调用 `buildExpression()` 构建参数，并把此前的 base 移入 `CallExpr::callee_`。

`f()` 的参数列表为空，`f(1, 2,)` 保留两个参数，尾逗号不产生额外参数。callee 使用表达式指针，因此支持 `f()(x)`、`a[i](x)` 和 `f(x)[i]` 的组合。

参数使用普通表达式规则，即使调用本身在条件中也是如此。`obj.method()` 属于 `dotSuffix`，没有随普通调用一起实现。

主要提交：`fce1425`、`0ce058d`、`bdd93cc`。

## 14. 实现请求：完成运算符的 AST 构建

算术、比较、逻辑和普通赋值已有基础构建，本次补齐位运算、移位、复合赋值、解引用、借用及 `as` 转换。

二元链左结合；普通和复合赋值通过右侧表达式递归保持右结合；前缀运算由内向外嵌套；连续 `as` 左结合。

新增 `CompoundAssignExpr` 保留复合赋值的目标和原运算符，`CastExpr` 的右侧保存类型。`&&mut x` 拆成两层借用，引用类型也保留对应结构。

移位与跳转操作数的解析边界还需要修改语法，具体过程见[问题与弯路](lessons.md)。

主要提交：`5293204`、`5aacabd`、`4def470`、`f4d4a04`。

## 15. 流程请求：分类分模块提交

按 AST 节点、Builder、语法及生成文件、测试等模块拆分提交。对新增支持必须同步调整的旧测试，可以和 Builder 一起提交；新增测试集再单独提交。

功能修改完成后先运行相应验证，再提交；模块拆分不改变已经验证过的实现内容。

## 16. 流程请求：从仓库删除 `tests/`，本地保留并加入 `.gitignore`

使用 `git rm --cached` 取消跟踪，保留本地文件；在 `.gitignore` 添加 `/tests/`。官方测试原来是子模块，因此还需要移除 `.gitmodules` 中的对应登记。

本次按“取消跟踪”和“忽略目录”分成两个提交：`2131a9a`、`b4b57dc`。本地测试内容未改变，之后仍可运行本地测试命令；新克隆的仓库不会自动带上这些测试。
