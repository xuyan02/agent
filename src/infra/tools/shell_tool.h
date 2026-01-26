#pragma once

#include "interfaces/itool.h"

#include <string>

namespace agent {

class ShellTool final : public agent::ITool {
public:
  explicit ShellTool(int timeout_ms);

  std::string Name() const override { return "run_shell_command"; }

  agent::ToolResult Invoke(const std::string& tool_call_id,
                                     const std::string& arguments_json,
                                     const agent::ToolContext& ctx) override;

private:
  int timeout_ms_{60000};
};

} // namespace agent
