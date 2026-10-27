#pragma once

#include <iosfwd>
#include <string>
#include <string_view>

namespace rx::driver {

struct Options {
  std::string stage;
  std::string source;
  std::string output;
  bool dump_tree = false;
};

void print_usage(std::ostream &stream, std::string_view program);
bool parse_options(int argc, char **argv, Options &options);
int run(const Options &options);

}  // namespace rx::driver
