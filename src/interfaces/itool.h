#pragma once

#include "core/policy.h"
#include "core/tool_protocol.h"

#include <string>

namespace agent {

struct ToolContext {
  const agent::Policy& policy;
};

class ITool {
public:
  virtual ~ITool() = default;
  [[nodiscard]] virtual std::string Name() const = 0;
  virtual agent::ToolResult Invoke(const std::string& tool_call_id,
                                             const std::string& arguments_json,
                                             const ToolContext& ctx) = 0;
};

} // namespace agent
