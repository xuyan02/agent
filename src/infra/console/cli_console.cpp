#include "infra/console/cli_console.h"

#include <iostream>

namespace cpp_agent::infra::console {

void CliConsole::print_line(const std::string& s) {
  std::cout << s << "\n";
  std::cout.flush();
}

std::optional<std::string> CliConsole::read_line(const std::string& prompt) {
  std::cout << prompt;
  std::cout.flush();

  std::string line;
  if (!std::getline(std::cin, line)) return std::nullopt;
  return line;
}

} // namespace cpp_agent::infra::console
