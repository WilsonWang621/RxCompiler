# Commands for the C++ compiler. See README-EN.md or README-ZH.md.
COMPILER ?= ./target/compiler
BUILD_COMPILER ?= ./scripts/build.sh

# Build once before testing; built-in I/O is provided by REIMU.
BUILD = $(BUILD_COMPILER) && : > {runtime}

# Parse, resolve names and check types without generating IR.
SEMANTIC = $(COMPILER) --stage semantic {source}

# Emit LLVM IR; the test runner lowers it with Clang and executes it in REIMU.
IR = $(COMPILER) --stage ir {source} -o {output}

# The current compiler reports that code generation is not implemented.
CODEGEN = $(COMPILER) --stage codegen {source} -o {output}

# Build REIMU separately before running IR/codegen/optimization tests.
RUN = xmake run -P vendor/REIMU reimu --memory=256M --stack=1M \
    -f {output},{runtime} -o {stdout} -p {profile} 1>&2
