# Rx 编译器

[English](README-EN.md) | [简体中文](README-ZH.md)

使用 C++17、ANTLR 4.13.2 和 CMake 实现的 [Rx 语言](https://acmclasscourse-2025.github.io/rx-compiler-specification/)编译器，目前正在开发 AST 前端与语义分析基础设施。

## 当前进度

- 已接入词法分析、语法分析、语法错误诊断和 AST 打印；正在逐步扩展 AST 构建支持。
- AST 节点已有 `NodeId` 和 `SourceSpan`，用于关联外部语义结果与定位诊断。
- 已实现值／类型命名空间、名字绑定栈、作用域退出恢复和 `ScopeGuard`，以及符号信息、声明索引和语义结果表。
- `SemanticChecker` 第一轮收集顶层声明和关联项，第二轮解析声明类型、检查函数体并求值常量。
- `--stage ast` 构建并打印 AST；`--stage semantic` 进一步执行语义检查，诊断错误时返回 1。
- 已接入 `--stage ir` 和部分标量表达式的 LLVM IR 生成；`config.mk` 已配置 IR 测试链路，尚未支持全部语言功能。
- `--stage codegen` 保留命令行入口，当前会报告未实现并返回退出码 2；优化阶段同样尚未实现。

语义测试和 IR 测试分别运行；语义检查通过不代表对应语言功能已支持 IR 生成。

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

`tests/official/` 是随仓库配置的官方测试子模块；`tests/ast/`、`tests/custom/`、`tests/semantic/` 等本地自写测试默认由 Git 忽略。

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
| `SEMANTIC` | 解析、名字解析与类型检查，不生成 IR。 |
| `IR` | `$(COMPILER) --stage ir {source} -o {output}`，输出 LLVM IR。 |
| `CODEGEN` | 调用自研编译器的 codegen 入口，目前明确报告未实现。 |
| `RUN` | 用 REIMU 执行 RV32IM 汇编，保存输出和周期统计。 |

`make test` 默认使用 `STAGE=semantic`，通过 `scripts/test.py` 发现 `tests/` 下的 manifest 并调用自己的 C++ 编译器：

```sh
make test
make test FILTER=official:semantic:arrays
make test FILTER=custom VERBOSE=true
make test COMPILE_TIMEOUT=60
```

测试日志保存在 `target/tests/run-*`。`FILTER` 使用冒号连接目录层级，使用逗号分隔多个选择；留空表示不筛选。`VERBOSE=true` 显示每个用例。默认编译超时为 30 秒，运行超时为 10 秒，可通过 `COMPILE_TIMEOUT`、`RUN_TIMEOUT` 覆盖。`PYTHON` 默认 `python3`。

官方测试通过 `tests/official` 子模块提供；克隆后需执行 `git submodule update --init --recursive`。每个用例目录通过 `manifest.json` 声明源文件、阶段、预期编译结果及可选输入输出，格式见 `tests/official/manifest.schema.json`。官方 `lex`、`parse` 条目仍由运行器跳过。

本地 AST 回归套件放在被忽略的 `tests/ast/` 中，可运行：

```sh
make test-ast
```

该命令先构建编译器，再运行 `test_*builder.py` 套件并比对 AST 输出。目录不存在时会明确报错。

作用域单元测试位于 `tests/semantic/scope_test.cpp`，不由 `make test` 或 `make test-ast` 自动运行。当前测试仍引用旧字段名 `resolved_symbols`，与模型中的 `resolvedSymbols` 不一致，因此暂时无法编译；同步测试后可独立运行：

```sh
mkdir -p target/tests/unit
c++ -std=c++17 -Wall -Wextra -Wpedantic -Isrc/ast \
    tests/semantic/scope_test.cpp -o target/tests/unit/scope_test
./target/tests/unit/scope_test
```

`STAGE` 可显式设为 `semantic`、`ir`、`codegen` 或 `optimization`；`STAGE=` 表示选择全部阶段。IR 已配置；尚未实现的 IR 功能会报告生成失败，codegen 与 optimization 尚不能通过。原模板的 Rust 参考辅助代码仍保留在 `crates/rx`，默认测试命令已切换为自研编译器。

## IR 增量验证

```sh
make test STAGE=ir FILTER=ir                 # 本地标量冒烟测试（目录存在时）
make test STAGE=ir FILTER=official:codegen:builtin-io # CI 使用的官方冒烟测试
make test STAGE=ir                          # 全部 IR 用例，包含尚未实现的功能
```

运行器从 manifest 的 `codegen` 用例派生 IR 测试，无需修改官方 manifest。新增本地回归可放入被 Git 忽略的 `tests/ir/`，沿用 `stage: codegen` 和输入／预期输出格式。

每个 IR 用例依次执行以下检查，失败后停止该用例，继续其他用例：

| 日志／产物 | 定位 |
| --- | --- |
| `semantic.*` | 独立语义检查，失败说明前端未接受该用例。 |
| `compile.*`、`program.ll` | 语义检查通过后的 IR 生成，未实现功能也在此报错。 |
| `clang.*`、`program.s` | Clang 解析、验证并将 IR 编译为 RV32IM；失败需检查 IR 格式、类型或目标编译问题。 |
| `run-N.*` | REIMU 执行失败、超时或实际输出与预期不一致。 |

完整命令、标准输出和错误输出保存在 `target/tests/run-*/用例编号/`。`tests/ir/scalar.rx` 当前覆盖调用、整数运算、赋值、引用和移位的输出验证，以及比较指令的格式验证。每补齐一个功能，可增加相应输入／输出用例并在本地运行；CI 仅使用官方测试。

## IR 运行环境

开始验证生成代码时，初始化并单独构建 REIMU（需要 Python 3、xmake 和支持 C++23 的编译器）：

```sh
git submodule update --init --recursive
xmake f -y -P vendor/REIMU -m release -o target/reimu
xmake -y -P vendor/REIMU
```

更新 REIMU 后需重新构建。IR 测试另需 Clang 22，默认 `CLANG=clang-22`。IR 命令应写出 `.ll`，运行器将其编译为 RV32IM 汇编；codegen 应直接输出 RV32IM 汇编。两条路径随后调用 `RUN`。

REIMU 从全局 `main` 符号开始执行，`RUN` 的 `{stdout}`、`{profile}` 分别保存程序输出和周期统计。公共运行时汇编应由 `BUILD` 写入 `{runtime}`，导出函数遵循 ILP32 调用约定。收集周期统计时保留 `-p {profile}`，不要添加 `--silent`。

## CI/CD

GitHub Actions 工作流 `.github/workflows/test.yml` 会在每次 push、Pull Request 以及手动触发时签出官方测试子模块并构建 C++ 编译器。每次运行都会在 GitHub Actions Summary 中记录提交、运行链接和最终状态，并保留构建日志 90 天；成功构建的编译器产物保留 30 天，可作为 Code Review 的 CI/CD 记录。

提交 Code Review 时应附上对应的 GitHub Actions Run 链接，并确保 `Build compiler` 检查通过。建议在 GitHub 分支保护规则中将该检查设为合并前必需。

CI 运行编译器构建、全部语义测试和 `FILTER=official:codegen:builtin-io` 的官方 IR 冒烟测试，并安装 Clang 22、xmake 和构建 REIMU。各步骤状态写入 Summary；语义、IR、REIMU 构建日志及每个用例的中间产物保留 90 天。完整官方 IR 套件尚不作为通过门槛，可在本地逐功能运行。
