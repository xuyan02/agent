#include "runtime/general_agent.h"

#include "runtime/runtime.h"

#include <iostream>
#include <utility>

namespace agent {

void GeneralAgent::OnToolCalls(std::vector<LlmToolCall> tool_calls) {
  if (tool_calls.empty()) return;

  // Persist assistant tool_calls into history for next round.
  LlmMessage assistant;
  assistant.role = LlmRole::kAssistant;
  assistant.tool_calls = tool_calls;
  llm_history_.push_back(std::move(assistant));

  ExecuteToolCalls(std::move(tool_calls));
}

void GeneralAgent::ExecuteToolCalls(std::vector<LlmToolCall> tool_calls) {
  pending_tool_calls_ = std::move(tool_calls);
  pending_tool_call_idx_ = 0;
  ExecuteNextToolCall();
}

static std::string build_tool_error_json(const std::string& msg) {
  // OpenAI-compatible: tool message content is a string. We embed a JSON object as a string.
  std::string out = "{\"error\":\"";
  for (char c : msg) {
    if (c == '"' || c == '\\') out.push_back('\\');
    out.push_back(c);
  }
  out += "\"}";
  return out;
}

void GeneralAgent::ExecuteNextToolCall() {
  if (pending_tool_call_idx_ >= pending_tool_calls_.size()) {
    pending_tool_calls_.clear();
    pending_tool_call_idx_ = 0;

    // All tools done; start next round with accumulated history.
    StartRound(llm_history_);
    return;
  }

  const LlmToolCall tc = pending_tool_calls_[pending_tool_call_idx_];
  pending_tool_call_idx_++;

  // Find the function by name.
  auto tools = GetTools();
  FunctionPtr fn;
  for (const auto& t : tools) {
    for (const auto& f : t.functions) {
      if (f && f->spec().name == tc.name) {
        fn = f;
        break;
      }
    }
    if (fn) break;
  }

  if (!fn) {
    LlmMessage tool_msg;
    tool_msg.role = LlmRole::kTool;
    tool_msg.tool_result = LlmToolResult{.tool_call_id = tc.id,
                                         .content = build_tool_error_json("Unknown tool: " + tc.name)};
    llm_history_.push_back(std::move(tool_msg));

    ExecuteNextToolCall();
    return;
  }

  std::string args = tc.arguments_json;
  std::string tool_call_id = tc.id;

  fn->InvokeAsync(std::move(args), [this, tool_call_id = std::move(tool_call_id)](std::string out_result_json, std::string out_error) {
    LlmMessage tool_msg;
    tool_msg.role = LlmRole::kTool;

    if (!out_error.empty()) {
      tool_msg.tool_result = LlmToolResult{.tool_call_id = tool_call_id,
                                           .content = build_tool_error_json(out_error)};
    } else {
      tool_msg.tool_result = LlmToolResult{.tool_call_id = tool_call_id,
                                           .content = std::move(out_result_json)};
    }

    llm_history_.push_back(std::move(tool_msg));
    ExecuteNextToolCall();
  });
}

} // namespace agent
