# IR integration validation

Local verification on 2026-11-12 covers implementation commit `6f783a9` and the accompanying documentation, Makefile and CI updates. The default executable at `target/compiler` was rebuilt in Release mode.

| Check | Command | Result |
| --- | --- | --- |
| Release build | `make build` | Passed |
| Semantic tests | `make test STAGE=semantic VERBOSE=true BUILD=` | 236 official cases passed |
| Official I/O IR smoke tests | `make test STAGE=ir FILTER=official:codegen:builtin-io VERBOSE=true BUILD=` | 2 cases, 2 runtime checks passed |
| Official control-flow IR regressions | `make test-ir-control-flow VERBOSE=true BUILD=` | 2 cases, 4 runtime checks passed |
| Workflow validation | YAML parsing, Bash syntax checks and step-outcome reference checks | Passed |
| Test tracking policy | `git ls-files tests` and ignore checks for local fixtures | Only `tests/official` is tracked |

The control-flow target selects `official:codegen:blocks-if-and-never` and `official:codegen:loops-and-jumps`. It checks branch results, early returns, condition reevaluation, nested loops, break values and continue targets. Each IR case passes semantic analysis, LLVM IR generation, Clang 22 compilation to RV32IM, and REIMU output comparison using the commands in `config.mk`.

Local logs are retained at `target/ci/build.log`, `target/ci/semantic.log`, `target/ci/ir.log` and `target/ci/control-flow.log`; per-case commands and intermediate files are under `target/tests/run-*`. Build products and local test fixtures remain ignored by Git.

The workflow now runs the official control-flow target and includes its outcome and log in the GitHub Actions summary and uploaded CI records. No remote workflow was triggered for this local commit; this document records local verification, not a GitHub Actions run. Attach the remote run link when submitting a code review after pushing.

These results cover the selected implemented features. They do not establish support for the full Rx language; the remaining IR limitations are listed in both READMEs.
