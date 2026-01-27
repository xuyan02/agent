#pragma once

#include "runtime/function.h"

#include <string>
#include <vector>

namespace agent {

struct Tool {
  // Activation unit.
  std::string id;
  std::string description;
  std::vector<FunctionPtr> functions;
};

} // namespace agent
