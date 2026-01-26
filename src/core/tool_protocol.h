#pragma once

#include <optional>
#include <string>
#include <vector>

namespace agent {

enum class Role {
  kSystem,
  kUser,
  kAssistant,
  kTool,
};

struct ToolCall {
  std::string id;
  std::string name;
  std::string arguments_json; // raw JSON string
};

struct ToolResult {
  std::string tool_call_id;
  bool ok{true};
  std::string content;
};

struct Message {
  Role role{Role::kUser};
  std::string content;

  // For assistant messages that request tool calls.
  std::vector<ToolCall> tool_calls;

  // For tool messages (result of a tool call)
  std::optional<ToolResult> tool_result;
};

} // namespace agent
