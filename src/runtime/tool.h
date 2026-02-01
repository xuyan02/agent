#pragma once

#include "runtime/function.h"

#include <memory>
#include <string>
#include <vector>

namespace agent {

struct Tool {
  // Activation unit.
  std::string id;
  std::string description;
  std::vector<FunctionPtr> functions;

  Function* FindFunctionByName(const std::string& name) const;
};

using ToolPtr = std::unique_ptr<Tool>;

}  // namespace agent
