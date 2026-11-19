# Commands for the C++ compiler. See README-EN.md or README-ZH.md.
COMPILER ?= ./target/compiler
BUILD_COMPILER ?= ./scripts/build.sh

# Execution stages link the C runtime. Semantic-only tests need just the compiler.
RUNTIME_BUILD = $(CLANG) --target=riscv32-unknown-none-elf -march=rv32im -mabi=ilp32 \
    -O2 -fno-builtin -mllvm -riscv-no-aliases -S src/runtime/runtime.c -o {runtime}
BUILD = $(BUILD_COMPILER) $(if $(filter semantic,$(strip $(STAGE))),,&& $(RUNTIME_BUILD))

# Parse, resolve names and check types without generating IR.
SEMANTIC = $(COMPILER) --stage semantic {source}

# Emit LLVM IR; the test runner lowers it with Clang and executes it in REIMU.
IR = $(COMPILER) --stage ir {source} -o {output}

# The current compiler reports that code generation is not implemented.
CODEGEN = $(COMPILER) --stage codegen {source} -o {output}

# Build REIMU separately before running IR/codegen/optimization tests.
RUN = xmake run -P vendor/REIMU reimu --memory=256M --stack=1M \
    -f {output},{runtime} -o {stdout} -p {profile} 1>&2
