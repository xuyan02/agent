#pragma once

#include <optional>
#include <string>
#include <vector>

namespace agent {

enum class LlmRole {
  kSystem,
  kUser,
  kAssistant,
  kTool,
};

struct LlmToolCall {
  std::string id;
  std::string name;
  // Raw JSON object string (no surrounding quotes).
  std::string arguments_json;
};

struct LlmToolResult {
  std::string tool_call_id;
  // OpenAI-compatible: tool message content is a string (can contain JSON text).
  std::string content;
};

struct LlmMessage {
  LlmRole role{LlmRole::kUser};
  std::string content;

  // For assistant messages that request tool calls.
  std::vector<LlmToolCall> tool_calls;

  // For tool messages (result of a tool call).
  std::optional<LlmToolResult> tool_result;
};

} // namespace agent
