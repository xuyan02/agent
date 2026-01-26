#pragma once

#include "interfaces/itool.h"

#include <filesystem>
#include <string>

namespace agent {

class ReadFileTool final : public agent::ITool {
public:
  std::string Name() const override { return "read_file"; }

  agent::ToolResult Invoke(const std::string& tool_call_id,
                                     const std::string& arguments_json,
                                     const agent::ToolContext& ctx) override;
};

class WriteFileTool final : public agent::ITool {
public:
  std::string Name() const override { return "write_file"; }

  agent::ToolResult Invoke(const std::string& tool_call_id,
                                     const std::string& arguments_json,
                                     const agent::ToolContext& ctx) override;
};

} // namespace agent
