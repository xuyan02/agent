#pragma once

#include "infra/llm/llm_message.h"

#include <optional>
#include <string>
#include <vector>

namespace agent {

struct OpenAIStreamDelta {
  std::string content_delta;
  bool has_finish_reason{false};
};

// Aggregates OpenAI streaming tool_calls deltas into a complete assistant message.
class OpenAIStreamAccumulator {
public:
  void Reset();

  // Feed a single SSE "data: {...}" json line (already stripped to JSON object string).
  // Returns false on malformed input (best-effort parser).
  bool FeedDataLine(const std::string& data_line, OpenAIStreamDelta* out_delta);

  // Whether we have any tool calls accumulated for current round.
  bool HasToolCalls() const;

  // Produce the assistant message for the current round (content + tool_calls).
  LlmMessage BuildAssistantMessage() const;

private:
  struct ToolCallState {
    std::string id;
    std::string name;
    std::string arguments_json;
    bool arguments_complete{false};
  };

  std::string content_;
  // index -> state
  std::vector<ToolCallState> tool_calls_;

  void EnsureToolIndex(size_t idx);
};

} // namespace agent
