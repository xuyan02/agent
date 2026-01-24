#pragma once

#include "interfaces/iconsole.h"

namespace cpp_agent::infra::console {

class CliConsole final : public cpp_agent::interfaces::IConsole {
public:
  void print_line(const std::string& s) override;
  std::optional<std::string> read_line(const std::string& prompt) override;
};

} // namespace cpp_agent::infra::console
