#pragma once

#include "tool/tool.h"

#include <string>

namespace agent {

class ToolProvider {
 public:
  virtual ~ToolProvider() = default;

  virtual agent::Tool* FindTool(const std::string& tool_id) const = 0;
};

}  // namespace agent
