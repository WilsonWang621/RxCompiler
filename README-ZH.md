# Rx 编译器

[English](README-EN.md) | [简体中文](README-ZH.md)

使用 C++17、ANTLR 4.13.2 和 CMake 实现的 [Rx 语言](https://acmclasscourse-2025.github.io/rx-compiler-specification/)编译器，目前正在开发 AST 前端与语义分析基础设施。

## 当前进度

- 已接入词法分析、语法分析、语法错误诊断和 AST 打印；正在逐步扩展 AST 构建支持。
- AST 节点已有 `NodeId` 和 `SourceSpan`，用于关联外部语义结果与定位诊断。
- 已实现值／类型命名空间、名字绑定栈、作用域退出恢复和 `ScopeGuard`，以及符号信息、声明索引和语义结果表。
- `SemanticChecker` 的第一轮扫描已收集顶层函数、常量、结构体及 `impl` 关联项，并诊断重复声明、参数、字段及无效 `impl` 目标。第二轮扫描仍为空；函数体名字解析、类型检查和常量求值尚未实现。
- `--stage ast` 与 `--stage semantic` 目前均只解析源码、构建 AST 并打印到标准输出。`semantic_checker.cpp` 尚未加入 CMake 编译目标，命令行也尚未调用 `SemanticChecker`。
- LLVM IR 尚未实现，`config.mk` 中的 `IR` 留空。
- `--stage codegen` 保留命令行入口，当前会报告未实现并返回退出码 2；优化阶段同样尚未实现。

官方 semantic 测试中的拒绝用例会暴露缺失的语义检查；部分合法用例也可能因 AST 尚未支持对应语法而失败。现阶段的成功结果只表示通过了当前前端入口，不能据此认定完整语义正确。

## 项目结构

```text
src/
  main.cpp          命令行入口与前端流程
  ast/              AST 节点、NodeId、源码范围与打印
  frontend/         ANTLR 解析树到 AST 的转换
  semantic/         符号、作用域、声明收集与语义结果
grammar/            ANTLR 词法与语法定义
generated/          已生成的 ANTLR C++ 代码
scripts/            构建、解析器生成与测试运行器
tests/              本地测试与用例，不放在 src/ 中
  ast/              Python AST 回归测试
  semantic/         C++ 作用域单元测试
  official/         官方测试用例与 manifest
  custom/           自定义测试用例与 manifest
target/             编译产物和测试日志
```

`tests/` 当前被 Git 忽略，以上测试子目录描述的是本地布局，不保证克隆后存在。

## 构建与运行

构建需要支持 C++17 的编译器、CMake、Make、curl 和 `sha256sum`；测试运行器需要 Python 3。当前前端构建与测试无需 Rust、Clang 22 或 REIMU。

在项目根目录执行：

```sh
make build
# 可选：调整构建并行度
make build BUILD_JOBS=4
./target/compiler --stage ast path/to/program.rx
./target/compiler --stage semantic path/to/program.rx
./target/compiler --stage ast path/to/program.rx --dump-tree
```

构建入口为 `scripts/build.sh`，使用 Release 配置，构建目录为 `target/build`，可执行文件为 `target/compiler`。脚本会在 `.antlr/` 缺少 ANTLR C++ runtime 4.13.2 压缩包时下载并校验 SHA-256。

`--dump-tree` 会先打印 ANTLR 解析树，再打印 AST。语法错误返回 1；参数错误、输入文件无法打开、AST 构建异常或未实现阶段返回 2。

修改 `grammar/` 中的文法后，需要 Java，并将 ANTLR 4.13.2 工具放在 `.antlr/antlr-4.13.2-complete.jar`，然后执行：

```sh
make generate-parser
make build
```

生成的 C++ 文件已经包含在 `generated/`，普通构建不需要 Java，也不会自动重新生成解析器。

## 语义基础设施

AST 保存语法结构、节点 ID 与源码位置；语义结果通过 ID 存在外部表中，不直接增加到各类 AST 节点上。

| 文件 | 职责与当前状态 |
| --- | --- |
| `src/ast/node_id.hpp` | 分配节点 ID，`0` 保留为无效 ID；`ASTNode::id()` 提供访问。 |
| `src/ast/source_span.hpp` | 源码范围与范围合并；ASTBuilder 在构建节点时填写。 |
| `src/semantic/symbol.hpp` | `SymbolId`、符号种类及名字、声明节点、源码位置、可变性等符号信息。 |
| `src/semantic/scope.hpp` | 分开的值／类型名字绑定栈、作用域撤销日志和自动进入／退出作用域的 `ScopeGuard`。 |
| `src/semantic/declaration_table.hpp` | 顶层名字到符号的索引，以及以 `SymbolId` 为键的函数、常量、结构体和关联项信息。 |
| `src/semantic/semantic_model.hpp` | 持久保存声明、符号、节点语义结果及诊断。`semantic_model.cpp` 目前为空。 |
| `src/semantic/semantic_checker.hpp`、`.cpp` | `check()` 先收集声明，再进入尚待实现的第二轮检查。 |
| `src/semantic/type.hpp` | 预留给语义类型系统，目前为空；`TypeId` 暂时定义在 `semantic_model.hpp` 中。 |

`SourceSpan` 使用左闭右开区间 `[begin, end)`：行列从 1 开始，`offset` 从 0 开始、按 Unicode 码点计数，而不是 UTF-8 字节。当前位置相对于单个输入文件，不保存文件名；默认范围表示未知位置。

`Scope` 只回答“当前这个名字绑定到哪个 `SymbolId`”，不保存类型对象或永久符号信息。`values_` 与 `types_` 分别保存值名与类型名的绑定栈，查找取栈顶，退出作用域按撤销日志逆序恢复。默认拒绝同层重名，传入 `allowShadowing=true` 可允许同层遮蔽；是否允许遮蔽特定符号仍由语义检查器决定。

`SemanticModel` 的主要映射为：

- `symbols`：`SymbolId -> SymbolInfo`；`declaredSymbols`：声明节点 `NodeId -> SymbolId`。
- `resolvedSymbols`：名字使用节点 `NodeId -> SymbolId`；`expressionTypes`、`resolvedTypes` 分别保存表达式和类型引用的类型 ID。
- `constantValues`、`expressionInfo` 预留常量值与表达式属性；`diagnostics` 保存源码范围和诊断消息。

目前第一轮扫描只填充 `declarations`、`symbols`、`declaredSymbols` 和 `diagnostics`，不产生未经检查的表达式结果。它先收集普通顶层声明，再处理 `impl`，使声明索引不受源码先后顺序限制。声明表持有不拥有 AST 的指针，因此对应 AST 必须比 `SemanticModel` 活得更久；退出局部作用域只恢复名字绑定，不删除模型中的符号或解析结果。

## 测试与配置

`Makefile` 是统一入口，编译器命令配置在 `config.mk`：

| 配置 | 当前行为 |
| --- | --- |
| `COMPILER` | 默认 `./target/compiler`，可覆盖可执行文件路径。 |
| `BUILD_COMPILER` | 默认 `./scripts/build.sh`，供 `make build` 和测试构建使用。 |
| `BUILD` | 构建 C++ 编译器一次，并保留空的 `{runtime}` 文件。 |
| `SEMANTIC` | 调用当前解析与 AST 构建入口。 |
| `IR` | 留空；选中 IR 测试时运行器提示先配置该命令。 |
| `CODEGEN` | 调用自研编译器的 codegen 入口，目前明确报告未实现。 |
| `RUN` | 保留 REIMU 命令，供后续 IR、codegen 和 optimization 使用。 |

`make test` 默认使用 `STAGE=semantic`，通过 `scripts/test.py` 发现 `tests/` 下的 manifest 并调用自己的 C++ 编译器：

```sh
make test
make test FILTER=official:semantic:arrays
make test FILTER=custom VERBOSE=true
make test COMPILE_TIMEOUT=60
```

测试日志保存在 `target/tests/run-*`。`FILTER` 使用冒号连接目录层级，使用逗号分隔多个选择；留空表示不筛选。`VERBOSE=true` 显示每个用例。默认编译超时为 30 秒，运行超时为 10 秒，可通过 `COMPILE_TIMEOUT`、`RUN_TIMEOUT` 覆盖。`PYTHON` 默认 `python3`。

测试目录当前被 `.gitignore` 忽略，本地已有测试不会随普通克隆带入；运行前需准备官方或自定义用例。每个用例目录通过 `manifest.json` 声明源文件、阶段、预期编译结果及可选输入输出，格式见本地的 `tests/official/manifest.schema.json`。官方 `lex`、`parse` 条目仍由运行器跳过。

本地 `tests/ast/` 已有 AST 回归套件时，可运行：

```sh
make test-ast
```

该命令先构建编译器，再运行 `test_*builder.py` 套件并比对 AST 输出；这些本地测试同样尚未纳入 Git。目录不存在时会明确报错。

作用域单元测试位于 `tests/semantic/scope_test.cpp`，不由 `make test` 或 `make test-ast` 自动运行。当前测试仍引用旧字段名 `resolved_symbols`，与模型中的 `resolvedSymbols` 不一致，因此暂时无法编译；同步测试后可独立运行：

```sh
mkdir -p target/tests/unit
c++ -std=c++17 -Wall -Wextra -Wpedantic -Isrc/ast \
    tests/semantic/scope_test.cpp -o target/tests/unit/scope_test
./target/tests/unit/scope_test
```

`STAGE` 可显式设为 `semantic`、`ir`、`codegen` 或 `optimization`；`STAGE=` 表示选择全部阶段。目前 IR 未配置，codegen 与 optimization 尚不能通过，待实现后再启用。原模板的 Rust 参考辅助代码仍保留在 `crates/rx`，默认测试命令已切换为自研编译器。

## 后续运行环境

开始验证生成代码时，初始化并单独构建 REIMU（需要 Python 3、xmake 和支持 C++23 的编译器）：

```sh
git submodule update --init --recursive
xmake f -y -P vendor/REIMU -m release -o target/reimu
xmake -y -P vendor/REIMU
```

更新 REIMU 后需重新构建。IR 测试另需 Clang 22，默认 `CLANG=clang-22`。IR 命令应写出 `.ll`，运行器将其编译为 RV32IM 汇编；codegen 应直接输出 RV32IM 汇编。两条路径随后调用 `RUN`。

REIMU 从全局 `main` 符号开始执行，`RUN` 的 `{stdout}`、`{profile}` 分别保存程序输出和周期统计。公共运行时汇编应由 `BUILD` 写入 `{runtime}`，导出函数遵循 ILP32 调用约定。收集周期统计时保留 `-p {profile}`，不要添加 `--silent`。

## CI

前端与 AST 开发期间，测试工作流已改名为 `.github/workflows/test.yml.disabled`，因此不会在提交或 PR 时自动运行。工作流中的依赖与命令已切换为 C++ 构建和 semantic 测试。语义分析具备稳定回归结果、且 CI 中已准备好测试用例后，可改回 `test.yml`；启用 IR 或后端测试时，再补充 Clang、xmake 和 REIMU 构建步骤。
