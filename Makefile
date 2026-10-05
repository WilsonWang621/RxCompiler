.DEFAULT_GOAL := test

include config.mk

PYTHON ?= python3
BUILD_JOBS ?= 2
VERBOSE ?= false
# Directory paths below tests, joined with ':'; separate selections with ','.
FILTER ?=
# Default to the current frontend entrypoint. Set STAGE= for all stages.
# Semantic tests still expose missing name resolution and type checking.
STAGE ?= semantic
CLANG ?= clang-22
COMPILE_TIMEOUT ?= 30
RUN_TIMEOUT ?= 10

# Export commands as data, so shell quoting survives Make's recipe expansion.
export RX_TEST_BUILD = $(BUILD)
export RX_TEST_SEMANTIC = $(SEMANTIC)
export RX_TEST_IR = $(IR)
export RX_TEST_CODEGEN = $(CODEGEN)
export RX_TEST_RUN = $(RUN)
export FILTER STAGE CLANG COMPILE_TIMEOUT RUN_TIMEOUT VERBOSE
export BUILD_JOBS

.PHONY: build generate-parser test test-ast
build:
	@$(BUILD_COMPILER)

generate-parser:
	@./scripts/generate_parser.sh

test:
	@$(PYTHON) scripts/test.py

# Local AST suites are currently ignored by Git and may be absent in a clone.
test-ast: build
	@test -d tests/ast || { echo "error: local AST tests are missing (tests/ast)" >&2; exit 2; }
	@RX_AST_COMPILER="$(abspath $(COMPILER))" $(PYTHON) -m unittest discover -s tests/ast -p 'test_*builder.py'
