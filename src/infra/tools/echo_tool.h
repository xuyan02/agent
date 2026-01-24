#pragma once

#include "interfaces/itool.h"

namespace cpp_agent::infra::tools {

class EchoTool final : public cpp_agent::interfaces::ITool {
public:
  std::string name() const override { return "echo"; }

  cpp_agent::core::ToolResult invoke(const std::string& tool_call_id,
                                     const std::string& arguments_json,
                                     const cpp_agent::interfaces::ToolContext& ctx) override;
};

} // namespace cpp_agent::infra::tools
