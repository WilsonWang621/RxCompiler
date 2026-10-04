# 问题与弯路

本页记录实际遇到的调试问题。理解性提问放在[提问与答案](questions-and-answers.md)，设计取舍放在[设计改进](design-decisions.md)。这里的记录属于共同开发过程，不将每个问题归为提问者的错误。

## 1. `break` 的移位操作数提前结束

发现于 2026-10-05 的运算符构建与测试，修复提交：`5aacabd`。

触发样例：

```rust
fn main() {
    while break a << b {}
}
```

这是用来观察 AST 的样例，循环归属及条件类型的合法性要另做语义检查。期望移位属于 `break` 的操作数：

```text
BreakExpr
 BinaryExpr: <<
  PathExpr: a
  PathExpr: b
```

最初实际得到：

```text
BinaryExpr: <<
 BreakExpr
  PathExpr: a
 PathExpr: b
```

用 `--dump-tree` 检查后，确认解析树已经把移位放在 `break` 外面，Builder 只是忠实转换了这棵树。问题发生在语法解析阶段。

本次走过的弯路：先只把 `conditionBreakShiftExpression` 和对应 closed 规则中的移位链分支放到单操作数分支前面。重新生成、构建后，边界测试仍失败；外层移位规则仍会优先取走运算符。

最终一起调整两层规则：普通和条件的移位规则先尝试完整的基础操作数，再尝试移位链；条件 `break` 的专用规则优先匹配移位链。这样内层跳转表达式能消费完整操作数，同时继续保留 closed 规则对 `as` 后 `<`、`<<` 的限制。

修改 `grammar/Parser.g4` 后，通过 `scripts/generate_parser.sh` 同步生成文件。验证覆盖了：

- `while break a << b {}` 和交错移位链。
- `if break a >> b << c {}`。
- `while break a >> b << c < d {}`。
- 普通表达式及条件中的 `return`、`break` 移位操作数。
- 原有 `while break {}` 的循环体边界。

记住：AST 形状不对时，先对照解析树定位问题所属阶段；不要在 Builder 中重新拼接节点来掩盖错误的解析边界。

## 2. 新功能接通后，旧的“不支持”测试需要同步更新

调用构建前，旧测试把 `f()`、`a[i]()` 和字段、方法访问一起列为不支持。支持调用后，这两个拒绝断言已失效；`a.field`、`a.method()` 仍由尚未实现的 `dotSuffix` 处理。

运算符接通前，旧测试也会拒绝 `as`、复合赋值、位运算和移位。新增支持后，移除这些旧拒绝断言，补上树形结构、优先级、结合性和错误语法测试。

不能直接把所有失败的负向测试删掉。需要区分：新增支持的输入、仍未支持的输入，以及真正的语法错误。例如 `f(1,,2)` 和 `a > > b` 仍应在解析阶段失败。

相关提交：`0ce058d`、`bdd93cc`、`4def470`、`f4d4a04`。

## 3. 测试脚本读文本时改变了原始换行

验证语法修改时，临时脚本使用 `Path.read_text()` 读取官方测试，再写入包装后的源文件。它会进行通用换行转换，原文件中的裸 `\r` 被转换成 `\n`。

官方样例 `lex-bare-cr-nondoc-comment-54c10d71e6.rx` 专门检查注释中的裸回车。这一转换改变了词法输入，使修改前后的编译器都出现同一个测试失败。

直接读取原文件编译后，两版都能通过解析，说明问题在验证脚本。改为按字节读取和写回，保留原始换行：

```python
source = source_path.read_bytes().decode("utf-8")
case_path.write_bytes(source.encode("utf-8"))
```

再次验证，442 项官方语法用例全部通过，修改前后的接受与拒绝结果一致。

记住：验证词法规则时，保留源文件的字节内容；不要让测试包装过程悄悄修改要测的输入。

## 4. 取消跟踪测试子模块前，Git 要求暂存配置变更

本次需要把 `tests/` 移出仓库、保留本地文件并加入 `.gitignore`。其中 `tests/official` 原来是子模块，其他测试是普通文件。

移除 `.gitmodules` 中的测试子模块登记后，直接执行 `git rm -r --cached -- tests/`，Git 提示先暂存 `.gitmodules` 的修改。

实际处理顺序：

```sh
git add .gitmodules
git rm -r --cached -- tests/
```

随后提交取消跟踪的变更，再单独提交 `.gitignore` 中的 `/tests/`。没有执行会清空本地目录的子模块反初始化操作。

验证包括：Git 不再跟踪 `tests/`，忽略规则生效，以及操作前后本地 1223 个文件的内容摘要一致。

相关提交：`2131a9a`、`b4b57dc`。这次删除影响当前仓库树，旧提交中仍保留历史测试内容。

## 5. AST 测试结果需要说明验证范围

当前 `--stage semantic` 会输出 AST，尚未执行完整语义分析。测试中出现 `1 += 2`、`true << false` 或未定义名称，仍可能成功构建节点；这不能证明它们是合法程序。

最近一次功能验证记录（2026-10-05，至 `f4d4a04`）：60 项 AST 测试通过，另有 442 项官方语法用例通过。后者的片段用例按 `metadata.entry` 包装后验证解析，并对比了语法修改前后的结果。两组结果分别证明 AST 结构和解析行为，不能替代后续语义测试。

本地测试命令：

```sh
cmake --build build --parallel 2
python3 -m unittest discover -s tests/ast -p 'test_*builder.py' -v
```

提交文档或调整忽略规则时，检查链接、Git 差异和本地文件保留情况即可；本页的功能测试结果是上述开发阶段的记录。
