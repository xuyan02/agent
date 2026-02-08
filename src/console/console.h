#pragma once

#include "dust/functional/function.h"

#include <string>

namespace agent {

class Console {
 public:
  virtual ~Console() = default;

  Console(const Console&) = delete;
  Console& operator=(const Console&) = delete;

  virtual void PrintLine(const std::string& s) = 0;
  virtual void Print(const std::string& s) = 0;

  // Called by the runner to receive complete input lines (without trailing newline).
  virtual void SetOnLine(dust::Function<void(std::string)> on_line) = 0;

 protected:
  Console() = default;
};

}  // namespace agent
