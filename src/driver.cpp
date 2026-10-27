#include "driver.hpp"

#include <antlr4-runtime.h>

#include <cstdlib>
#include <exception>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <utility>

#include "Lexer.h"
#include "Parser.h"
#include "ast_builder.hpp"
#include "ir_generator.hpp"
#include "semantic_checker.hpp"

namespace rx::driver {

void print_usage(std::ostream &stream, std::string_view program) {
  stream << "Usage:\n"
         << "  " << program << " --stage ast <source>\n"
         << "  " << program << " --stage semantic <source>\n"
         << "  " << program << " --stage ir <source> -o <output>\n"
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

  if (options.stage != "ast" && options.stage != "semantic" &&
      options.stage != "ir" && options.stage != "codegen") {
    std::cerr << "error: --stage must be ast, semantic, ir or codegen\n";
    return false;
  }
  if (options.source.empty()) {
    std::cerr << "error: no input file\n";
    return false;
  }
  if ((options.stage == "ir" || options.stage == "codegen") && options.output.empty()) {
    std::cerr << "error: " << options.stage << " requires -o <output>\n";
    return false;
  }
  return true;
}

namespace {

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

void print_diagnostics(const Options &options, const SemanticModel &model) {
  for (const auto &diagnosis : model.diagnostics) {
    std::cerr << options.source;
    if (diagnosis.span.begin.valid()) {
      std::cerr << ':' << diagnosis.span.begin.line
                << ':' << diagnosis.span.begin.column;
    }
    std::cerr << ": error: " << diagnosis.message << '\n';
  }
}

void write_ir(const IRModule &module, const std::string &path) {
  std::ofstream output(path);
  if (!output) {
    throw std::runtime_error("cannot open output file: " + path);
  }
  module.dump(output);
  output.close();
  if (!output) {
    throw std::runtime_error("cannot write output file: " + path);
  }
}

int compile_ast(const Options &options, rx::Parser::CrateContext *tree) {
  if (options.stage == "codegen") {
    throw std::runtime_error("code generation is not implemented yet");
  }

  rx::frontend::ASTBuilder builder;
  auto astRoot = builder.build(tree);
  if (options.stage != "ir") {
    astRoot->dump(std::cout);
  }
  if (options.stage == "ast") return 0;

  SemanticChecker checker;
  auto model = checker.check(*astRoot);
  print_diagnostics(options, model);
  if (!model.diagnostics.empty()) return 1;
  if (options.stage == "semantic") return 0;

  IRGenerator generator;
  auto module = generator.generate(*astRoot, model);
  write_ir(module, options.output);
  return 0;
}

}  // namespace

int run(const Options &options) {
  std::ifstream source_stream(options.source);
  if (!source_stream) {
    throw std::runtime_error("cannot open input file: " + options.source);
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

  return compile_ast(options, tree);
}

}  // namespace rx::driver
