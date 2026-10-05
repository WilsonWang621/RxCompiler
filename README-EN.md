# Rx Compiler

[English](README-EN.md) | [简体中文](README-ZH.md)

A [Rx language](https://acmclasscourse-2025.github.io/rx-compiler-specification/) compiler implemented with C++17, ANTLR 4.13.2 and CMake. Development currently focuses on the frontend and AST construction.

## Current status

- Lexing, parsing, syntax diagnostics and AST printing are wired up; AST construction support is being expanded incrementally.
- `--stage semantic` currently parses the source, builds an AST and prints it to stdout. Name resolution, type checking and other semantic passes are not implemented yet.
- LLVM IR generation is not implemented; `IR` in `config.mk` is empty.
- `--stage codegen` retains its CLI entrypoint but reports that code generation is not implemented and exits with code 2. Optimization is also not implemented.

Negative official semantic cases expose the missing semantic checks. Some positive cases may also fail when their syntax is not supported by the AST builder. A successful result currently demonstrates acceptance by the frontend entrypoint, not complete semantic correctness.

## Building and running

Building requires a C++17 compiler, CMake, Make, curl and `sha256sum`. The test runner requires Python 3. Rust, Clang 22 and REIMU are not needed for the current frontend build or tests.

Run from the project root:

```sh
make build
# Optional: adjust build parallelism
make build BUILD_JOBS=4
./target/compiler --stage semantic path/to/program.rx
./target/compiler --stage semantic path/to/program.rx --dump-tree
```

`scripts/build.sh` builds in Release mode using `target/build` and writes the executable to `target/compiler`. If the ANTLR C++ runtime 4.13.2 archive is missing from `.antlr/`, the script downloads it and verifies its SHA-256 checksum.

`--dump-tree` prints the ANTLR parse tree before the AST. Syntax errors exit with code 1. Invalid arguments, unreadable input files, AST construction exceptions and unimplemented stages exit with code 2.

After editing the grammar in `grammar/`, install Java and place the ANTLR 4.13.2 tool at `.antlr/antlr-4.13.2-complete.jar`, then run:

```sh
make generate-parser
make build
```

Generated C++ files are included in `generated/`. Ordinary builds do not require Java and do not regenerate the parser automatically.

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

The test directory is currently ignored by `.gitignore`, so local tests are not included in an ordinary clone. Supply official or custom fixtures before running tests. Each fixture directory uses `manifest.json` to declare sources, stages, expected compilation results and optional input/output pairs; see the local `tests/official/manifest.schema.json` for the format. Official `lex` and `parse` entries are still skipped by the runner.

When the local AST regression suites are available in `tests/ast/`, run:

```sh
make test-ast
```

This builds the compiler and runs the `test_*builder.py` suites, comparing AST output. These local suites are also not tracked in Git. A missing directory produces an explicit error.

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

## CI

During frontend and AST development, the test workflow is named `.github/workflows/test.yml.disabled`, so pushes and pull requests do not trigger it. Its dependencies and command now target the C++ build and semantic tests. Rename it back to `test.yml` once semantic regression results are stable and test fixtures are available in CI. Add Clang, xmake and REIMU build steps when enabling IR or backend tests.
