# Commands for the C++ compiler. See README-EN.md or README-ZH.md.
COMPILER ?= ./target/compiler
BUILD_COMPILER ?= ./scripts/build.sh

# Build once before testing; no shared runtime is emitted at the AST stage.
BUILD = $(BUILD_COMPILER) && : > {runtime}

# Currently parses the source, builds the AST and prints it to stdout.
# Name resolution and type checking are not implemented yet.
SEMANTIC = $(COMPILER) --stage semantic {source}

# Leave unset until the compiler supports --stage ir and emits LLVM IR.
IR =

# The current compiler reports that code generation is not implemented.
CODEGEN = $(COMPILER) --stage codegen {source} -o {output}

# Used only by future IR/codegen/optimization tests; build REIMU separately.
RUN = xmake run -P vendor/REIMU reimu --memory=256M --stack=1M \
    -f {output},{runtime} -o {stdout} -p {profile} 1>&2
