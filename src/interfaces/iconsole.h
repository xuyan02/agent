#pragma once

#include <optional>
#include <string>

namespace cpp_agent::interfaces {

class IConsole {
public:
  virtual ~IConsole() = default;
  virtual void print_line(const std::string& s) = 0;
  virtual std::optional<std::string> read_line(const std::string& prompt) = 0;
};

} // namespace cpp_agent::interfaces
