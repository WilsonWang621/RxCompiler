# Rx Compiler

[English](README-EN.md) | [简体中文](README-ZH.md)

A [Rx language](https://acmclasscourse-2025.github.io/rx-compiler-specification/) compiler implemented with C++17, ANTLR 4.13.2 and CMake. Development currently focuses on the AST frontend and semantic-analysis infrastructure.

## Current status

- Lexing, parsing, syntax diagnostics and AST printing are wired up; AST construction support is being expanded incrementally.
- AST nodes carry `NodeId` and `SourceSpan` for associating external semantic results and locating diagnostics.
- Separate value/type namespaces, name binding stacks, scope-exit restoration and `ScopeGuard` are implemented, alongside symbol information, declaration indexes and semantic result tables.
- The first `SemanticChecker` scan collects top-level functions, constants, structs and `impl` associated items, diagnosing duplicate declarations, parameters, fields and invalid `impl` targets. The second scan is still empty; function-body name resolution, type checking and constant evaluation are not implemented.
- Both `--stage ast` and `--stage semantic` currently only parse the source, build an AST and print it to stdout. `semantic_checker.cpp` is not yet included in the CMake target, and the CLI does not invoke `SemanticChecker`.
- LLVM IR generation is not implemented; `IR` in `config.mk` is empty.
- `--stage codegen` retains its CLI entrypoint but reports that code generation is not implemented and exits with code 2. Optimization is also not implemented.

Negative official semantic cases expose the missing semantic checks. Some positive cases may also fail when their syntax is not supported by the AST builder. A successful result currently demonstrates acceptance by the frontend entrypoint, not complete semantic correctness.

## Project layout

```text
src/
  main.cpp          CLI entrypoint and frontend pipeline
  ast/              AST nodes, NodeId, source spans and printing
  frontend/         ANTLR parse-tree to AST conversion
  semantic/         Symbols, scopes, declaration collection and results
grammar/            ANTLR lexer and parser definitions
generated/          Generated ANTLR C++ sources
scripts/            Build, parser generation and test runner
tests/              Local tests and fixtures, kept outside src/
  ast/              Python AST regression tests
  semantic/         C++ scope unit tests
  official/         Official fixtures and manifests
  custom/           Custom fixtures and manifests
target/             Build artifacts and test logs
```

`tests/official/` is the official testcase submodule configured with the repository. Locally written tests under `tests/ast/`, `tests/custom/`, and `tests/semantic/` are ignored by Git by default.

## Building and running

Building requires a C++17 compiler, CMake, Make, curl and `sha256sum`. The test runner requires Python 3. Rust, Clang 22 and REIMU are not needed for the current frontend build or tests.

Run from the project root:

```sh
make build
# Optional: adjust build parallelism
make build BUILD_JOBS=4
./target/compiler --stage ast path/to/program.rx
./target/compiler --stage semantic path/to/program.rx
./target/compiler --stage ast path/to/program.rx --dump-tree
```

`scripts/build.sh` builds in Release mode using `target/build` and writes the executable to `target/compiler`. If the ANTLR C++ runtime 4.13.2 archive is missing from `.antlr/`, the script downloads it and verifies its SHA-256 checksum.

`--dump-tree` prints the ANTLR parse tree before the AST. Syntax errors exit with code 1. Invalid arguments, unreadable input files, AST construction exceptions and unimplemented stages exit with code 2.

After editing the grammar in `grammar/`, install Java and place the ANTLR 4.13.2 tool at `.antlr/antlr-4.13.2-complete.jar`, then run:

```sh
make generate-parser
make build
```

Generated C++ files are included in `generated/`. Ordinary builds do not require Java and do not regenerate the parser automatically.

## Semantic infrastructure

The AST stores syntax, node IDs and source locations. Semantic results live in external tables keyed by IDs rather than additional fields on individual AST node classes.

| File | Responsibility and current status |
| --- | --- |
| `src/ast/node_id.hpp` | Allocates node IDs, reserving `0` as invalid; accessed through `ASTNode::id()`. |
| `src/ast/source_span.hpp` | Source ranges and range merging; ASTBuilder fills them during node construction. |
| `src/semantic/symbol.hpp` | `SymbolId`, symbol kinds, names, declaration nodes, source locations and mutability information. |
| `src/semantic/scope.hpp` | Separate value/type name binding stacks, scope undo logs and automatic scope entry/exit through `ScopeGuard`. |
| `src/semantic/declaration_table.hpp` | Top-level name-to-symbol indexes and function, constant, struct and associated-item information keyed by `SymbolId`. |
| `src/semantic/semantic_model.hpp` | Retains declarations, symbols, node semantic results and diagnostics. `semantic_model.cpp` is currently empty. |
| `src/semantic/semantic_checker.hpp`, `.cpp` | `check()` collects declarations before entering the unimplemented second scan. |
| `src/semantic/type.hpp` | Reserved for the semantic type system and currently empty; `TypeId` is temporarily defined in `semantic_model.hpp`. |

`SourceSpan` uses half-open ranges `[begin, end)`. Lines and columns are 1-based; `offset` is 0-based and counts Unicode code points, not UTF-8 bytes. Locations currently refer to a single input file without storing its filename. A default span denotes an unknown location.

`Scope` only answers which `SymbolId` a name currently refers to; it stores neither type objects nor persistent symbol information. `values_` and `types_` hold separate value-name and type-name binding stacks. Lookup reads the stack top, while scope exit restores bindings by replaying undo entries in reverse. Same-scope duplicates are rejected by default; `allowShadowing=true` permits same-scope shadowing. The semantic checker must still decide whether a particular symbol may be shadowed.

The main `SemanticModel` mappings are:

- `symbols`: `SymbolId -> SymbolInfo`; `declaredSymbols`: declaration-node `NodeId -> SymbolId`.
- `resolvedSymbols`: name-use-node `NodeId -> SymbolId`; `expressionTypes` and `resolvedTypes` store type IDs for expressions and type references respectively.
- `constantValues` and `expressionInfo` reserve space for constant values and expression properties; `diagnostics` stores source spans and diagnostic messages.

The first scan currently fills only `declarations`, `symbols`, `declaredSymbols` and `diagnostics`, without producing unchecked expression results. It collects ordinary top-level declarations before processing `impl` blocks, so declaration indexes do not depend on source order. Declaration-table pointers do not own the AST, which must outlive the corresponding `SemanticModel`. Exiting a local scope restores name bindings without deleting symbols or resolved results from the model.

## Tests and configuration

The root `Makefile` provides the common entrypoint. `config.mk` defines the compiler commands:

| Setting | Current behavior |
| --- | --- |
| `COMPILER` | Defaults to `./target/compiler`; override to select another executable. |
| `BUILD_COMPILER` | Defaults to `./scripts/build.sh`; used by `make build` and test builds. |
| `BUILD` | Builds the C++ compiler once and leaves an empty `{runtime}` file. |
| `SEMANTIC` | Invokes the current parsing and AST construction entrypoint. |
| `IR` | Empty; selecting IR tests prompts you to configure this command first. |
| `CODEGEN` | Invokes the custom compiler's codegen entrypoint, which currently reports that it is not implemented. |
| `RUN` | Retains the REIMU command for future IR, codegen and optimization tests. |

`make test` defaults to `STAGE=semantic`. `scripts/test.py` discovers manifests under `tests/` and invokes the project's C++ compiler:

```sh
make test
make test FILTER=official:semantic:arrays
make test FILTER=custom VERBOSE=true
make test COMPILE_TIMEOUT=60
```

Logs are saved under `target/tests/run-*`. `FILTER` uses colons between directory levels and commas between selections; an empty value selects all directories. `VERBOSE=true` reports individual cases. Compilation and execution timeouts default to 30 and 10 seconds; override them with `COMPILE_TIMEOUT` and `RUN_TIMEOUT`. `PYTHON` defaults to `python3`.

Official tests are provided by the `tests/official` submodule; run `git submodule update --init --recursive` after cloning. Each fixture directory uses `manifest.json` to declare sources, stages, expected compilation results and optional input/output pairs; see `tests/official/manifest.schema.json` for the format. Official `lex` and `parse` entries are still skipped by the runner.

Local AST regression suites can be placed in the ignored `tests/ast/` directory and run with:

```sh
make test-ast
```

This builds the compiler and runs the `test_*builder.py` suites, comparing AST output. These local suites are also not tracked in Git. A missing directory produces an explicit error.

The scope unit test is in `tests/semantic/scope_test.cpp` and is not run automatically by `make test` or `make test-ast`. It currently references the old `resolved_symbols` field instead of the model's `resolvedSymbols`, so compilation fails until the test is updated. Once synchronized, run it independently:

```sh
mkdir -p target/tests/unit
c++ -std=c++17 -Wall -Wextra -Wpedantic -Isrc/ast \
    tests/semantic/scope_test.cpp -o target/tests/unit/scope_test
./target/tests/unit/scope_test
```

Set `STAGE` explicitly to `semantic`, `ir`, `codegen` or `optimization`; `STAGE=` selects all stages. IR is currently unconfigured, and codegen/optimization cannot pass yet. Enable them as their implementations become available. The template's Rust reference helpers remain in `crates/rx`, but default test commands now use the custom compiler.

## Future runtime setup

Before validating generated programs, initialize and build REIMU separately. It requires Python 3, xmake and a C++23 compiler:

```sh
git submodule update --init --recursive
xmake f -y -P vendor/REIMU -m release -o target/reimu
xmake -y -P vendor/REIMU
```

Rebuild after updating REIMU. IR tests additionally require Clang 22, with `CLANG=clang-22` by default. The IR command must write `.ll`, which the runner lowers to RV32IM assembly. Codegen must write RV32IM assembly directly. Both paths then invoke `RUN`.

REIMU starts at the global `main` symbol. The `{stdout}` and `{profile}` placeholders in `RUN` save program output and cycle statistics. Write shared runtime assembly to `{runtime}` during `BUILD`; exported functions must follow the ILP32 calling convention. Keep `-p {profile}` and avoid `--silent` when collecting cycles.

## CI/CD

The GitHub Actions workflow at `.github/workflows/test.yml` checks out the official testcase submodule and builds the C++ compiler on every push, pull request, and manual invocation. Every run records its commit, run link, and final status in the GitHub Actions summary, retains build logs for 90 days, and retains the successfully built compiler for 30 days as a CI/CD record for code review.

Include the corresponding GitHub Actions run link with each code review and ensure that `Build compiler` passes. Configure that check as required in the GitHub branch protection rules.

CI currently requires only a successful compiler build. Once semantic analysis has stable regression results, add the official `make test STAGE=semantic` run. Add Clang, xmake, and REIMU setup when enabling IR or backend tests.
