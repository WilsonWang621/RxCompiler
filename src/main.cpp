#include <antlr4-runtime.h>

#include <exception>
#include <fstream>
#include <iostream>
#include <string>
#include <string_view>

#include "Lexer.h"
#include "Parser.h"
#include "ast_builder.hpp"

namespace {

struct Options {
  std::string stage;
  std::string source;
  std::string output;
  bool dump_tree = false;
};

void print_usage(std::ostream &stream, std::string_view program) {
  stream << "Usage:\n"
         << "  " << program << " --stage ast <source>\n"
         << "  " << program << " --stage semantic <source>\n"
         << "  " << program << " --stage codegen <source> -o <output>\n"
         << "Options:\n"
         << "  --dump-tree  Print the ANTLR parse tree\n";
}

bool parse_options(int argc, char **argv, Options &options) {
  for (int index = 1; index < argc; ++index) {
    const std::string_view argument(argv[index]);

    if (argument == "--stage") {
      if (++index >= argc) {
        std::cerr << "error: --stage requires a value\n";
        return false;
      }
      options.stage = argv[index];
    } else if (argument == "-o") {
      if (++index >= argc) {
        std::cerr << "error: -o requires a path\n";
        return false;
      }
      options.output = argv[index];
    } else if (argument == "--dump-tree") {
      options.dump_tree = true;
    } else if (argument == "-h" || argument == "--help") {
      print_usage(std::cout, argv[0]);
      std::exit(0);
    } else if (!argument.empty() && argument.front() == '-') {
      std::cerr << "error: unknown option: " << argument << '\n';
      return false;
    } else if (options.source.empty()) {
      options.source = argv[index];
    } else {
      std::cerr << "error: multiple input files are not supported\n";
      return false;
    }
  }

  if (options.stage != "ast" && options.stage != "semantic" && options.stage != "codegen") {
    std::cerr << "error: --stage must be ast, semantic or codegen\n";
    return false;
  }
  if (options.source.empty()) {
    std::cerr << "error: no input file\n";
    return false;
  }
  if (options.stage == "codegen" && options.output.empty()) {
    std::cerr << "error: codegen requires -o <output>\n";
    return false;
  }
  return true;
}

class DiagnosticErrorListener final : public antlr4::BaseErrorListener {
 public:
  explicit DiagnosticErrorListener(std::string source)
      : source_(std::move(source)) {}

  void syntaxError(antlr4::Recognizer * /*recognizer*/,
                   antlr4::Token * /*offending_symbol*/, std::size_t line,
                   std::size_t column, const std::string &message,
                   std::exception_ptr /*exception*/) override {
    ++error_count_;
    std::cerr << source_ << ':' << line << ':' << (column + 1)
              << ": error: " << message << '\n';
  }

  [[nodiscard]] std::size_t error_count() const { return error_count_; }

 private:
  std::string source_;
  std::size_t error_count_ = 0;
};

int run(const Options &options) {
  std::ifstream source_stream(options.source);
  if (!source_stream) {
    std::cerr << "error: cannot open input file: " << options.source << '\n';
    return 2;
  }

  antlr4::ANTLRInputStream input(source_stream);
  rx::Lexer lexer(&input);
  antlr4::CommonTokenStream tokens(&lexer);
  rx::Parser parser(&tokens);
  DiagnosticErrorListener diagnostics(options.source);

  lexer.removeErrorListeners();
  parser.removeErrorListeners();
  lexer.addErrorListener(&diagnostics);
  parser.addErrorListener(&diagnostics);

  auto *tree = parser.crate();
  if (diagnostics.error_count() != 0) {
    return 1;
  }

  if (options.dump_tree) {
    std::cout << tree->toStringTree(&parser) << '\n';
  }

  if (options.stage == "ast" || options.stage == "semantic") {
    // The semantic-analysis passes will be invoked here once implemented.
    rx::frontend::ASTBuilder builder;

    auto astRoot = builder.build(tree);

    astRoot->dump(std::cout);
    return 0;
  }

  // Parsing is wired up, but emitting fake assembly would hide a missing
  // backend and turn an implementation error into a confusing runtime error.
  std::cerr << "error: code generation is not implemented yet\n";
  return 2;
}

}  // namespace

int main(int argc, char **argv) {
  Options options;
  if (!parse_options(argc, argv, options)) {
    print_usage(std::cerr, argc > 0 ? argv[0] : "compiler");
    return 2;
  }

  try {
    return run(options);
  } catch (const std::exception &exception) {
    std::cerr << "error: " << exception.what() << '\n';
    return 2;
  }
}
