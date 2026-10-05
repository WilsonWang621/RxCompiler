# Rx 编译器

[English](README-EN.md) | [简体中文](README-ZH.md)

使用 C++17、ANTLR 4.13.2 和 CMake 实现的 [Rx 语言](https://acmclasscourse-2025.github.io/rx-compiler-specification/)编译器，目前处于前端与 AST 构建阶段。

## 当前进度

- 已接入词法分析、语法分析、语法错误诊断和 AST 打印；正在逐步扩展 AST 构建支持。
- `--stage semantic` 当前执行解析、构建 AST 并打印到标准输出；名字解析、类型检查等语义分析尚未实现。
- LLVM IR 尚未实现，`config.mk` 中的 `IR` 留空。
- `--stage codegen` 保留命令行入口，当前会报告未实现并返回退出码 2；优化阶段同样尚未实现。

官方 semantic 测试中的拒绝用例会暴露缺失的语义检查；部分合法用例也可能因 AST 尚未支持对应语法而失败。现阶段的成功结果只表示通过了当前前端入口，不能据此认定完整语义正确。

## 构建与运行

构建需要支持 C++17 的编译器、CMake、Make、curl 和 `sha256sum`；测试运行器需要 Python 3。当前前端构建与测试无需 Rust、Clang 22 或 REIMU。

在项目根目录执行：

```sh
make build
# 可选：调整构建并行度
make build BUILD_JOBS=4
./target/compiler --stage semantic path/to/program.rx
./target/compiler --stage semantic path/to/program.rx --dump-tree
```

构建入口为 `scripts/build.sh`，使用 Release 配置，构建目录为 `target/build`，可执行文件为 `target/compiler`。脚本会在 `.antlr/` 缺少 ANTLR C++ runtime 4.13.2 压缩包时下载并校验 SHA-256。

`--dump-tree` 会先打印 ANTLR 解析树，再打印 AST。语法错误返回 1；参数错误、输入文件无法打开、AST 构建异常或未实现阶段返回 2。

修改 `grammar/` 中的文法后，需要 Java，并将 ANTLR 4.13.2 工具放在 `.antlr/antlr-4.13.2-complete.jar`，然后执行：

```sh
make generate-parser
make build
```

生成的 C++ 文件已经包含在 `generated/`，普通构建不需要 Java，也不会自动重新生成解析器。

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
