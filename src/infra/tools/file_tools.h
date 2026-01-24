#pragma once

#include "interfaces/itool.h"

#include <filesystem>
#include <string>

namespace cpp_agent::infra::tools {

class ReadFileTool final : public cpp_agent::interfaces::ITool {
public:
  std::string name() const override { return "read_file"; }

  cpp_agent::core::ToolResult invoke(const std::string& tool_call_id,
                                     const std::string& arguments_json,
                                     const cpp_agent::interfaces::ToolContext& ctx) override;
};

class WriteFileTool final : public cpp_agent::interfaces::ITool {
public:
  std::string name() const override { return "write_file"; }

  cpp_agent::core::ToolResult invoke(const std::string& tool_call_id,
                                     const std::string& arguments_json,
                                     const cpp_agent::interfaces::ToolContext& ctx) override;
};

} // namespace cpp_agent::infra::tools
