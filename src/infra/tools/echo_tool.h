#pragma once

#include "interfaces/itool.h"

namespace agent {

class EchoTool final : public agent::ITool {
public:
  std::string Name() const override { return "echo"; }

  agent::ToolResult Invoke(const std::string& tool_call_id,
                                     const std::string& arguments_json,
                                     const agent::ToolContext& ctx) override;
};

} // namespace agent
