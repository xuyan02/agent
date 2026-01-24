#pragma once

#include "interfaces/itool.h"

#include <string>

namespace cpp_agent::infra::tools {

class ShellTool final : public cpp_agent::interfaces::ITool {
public:
  explicit ShellTool(int timeout_ms);

  std::string name() const override { return "run_shell_command"; }

  cpp_agent::core::ToolResult invoke(const std::string& tool_call_id,
                                     const std::string& arguments_json,
                                     const cpp_agent::interfaces::ToolContext& ctx) override;

private:
  int timeout_ms_{60000};
};

} // namespace cpp_agent::infra::tools
