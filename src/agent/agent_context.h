#pragma once

#include "runtime/tool.h"

#include <string>
#include <vector>

namespace agent {

class AgentContext {
 public:
  virtual ~AgentContext() = default;

  virtual std::string GetModelName() const = 0;
  virtual std::string GetSystemPrompt() const = 0;
  virtual std::vector<Tool> GetTools() const = 0;
};

}  // namespace agent
