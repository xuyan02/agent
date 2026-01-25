#pragma once

#include "dust/functional/function.h"

#include <string>

namespace cpp_agent::interfaces {

class IConsole {
public:
  virtual ~IConsole() = default;

  virtual void PrintLine(const std::string& s) = 0;
  virtual void Print(const std::string& s) = 0;

  // Non-blocking input.
  // Implementations should invoke the callback when a full line is available.
  virtual void SetOnLine(dust::Function<void(std::string)> on_line) = 0;
};

} // namespace cpp_agent::interfaces
