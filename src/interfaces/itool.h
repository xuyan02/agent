#pragma once

#include "core/policy.h"
#include "core/tool_protocol.h"

#include <string>

namespace cpp_agent::interfaces {

struct ToolContext {
  const cpp_agent::core::Policy& policy;
};

class ITool {
public:
  virtual ~ITool() = default;
  [[nodiscard]] virtual std::string name() const = 0;
  virtual cpp_agent::core::ToolResult invoke(const std::string& tool_call_id,
                                             const std::string& arguments_json,
                                             const ToolContext& ctx) = 0;
};

} // namespace cpp_agent::interfaces
