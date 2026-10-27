#include "driver.hpp"

#include <exception>
#include <iostream>

int main(int argc, char **argv) {
  rx::driver::Options options;
  if (!rx::driver::parse_options(argc, argv, options)) {
    rx::driver::print_usage(std::cerr, argc > 0 ? argv[0] : "compiler");
    return 2;
  }

  try {
    return rx::driver::run(options);
  } catch (const std::exception &exception) {
    std::cerr << "error: " << exception.what() << '\n';
    return 2;
  }
}
