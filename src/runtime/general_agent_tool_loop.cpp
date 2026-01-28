#include "runtime/general_agent.h"

#include "runtime/runtime.h"

#include <nlohmann/json.hpp>

#include <iostream>
#include <utility>

namespace agent {

void GeneralAgent::OnToolCalls(std::vector<LlmToolCall> tool_calls) {
  // sleep() is a special idle primitive; allow an empty tool_calls list but ignore it.
  if (tool_calls.empty()) return;

  std::cerr << "[cpp-agent.tool] got tool_calls n=" << tool_calls.size() << "\n";
  for (size_t i = 0; i < tool_calls.size(); i++) {
    const auto& tc = tool_calls[i];
    std::cerr << "[cpp-agent.tool] tool_call[" << i << "] id=" << tc.id << " name=" << tc.name
              << " args.len=" << tc.arguments_json.size() << "\n";
  }

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
  nlohmann::json j;
  j["error"] = msg;
  return j.dump();
}

void GeneralAgent::ExecuteNextToolCall() {
  if (pending_tool_call_idx_ >= pending_tool_calls_.size()) {
    pending_tool_calls_.clear();
    pending_tool_call_idx_ = 0;

    std::cerr << "[cpp-agent.tool] all tools done; starting next round (history msgs=" << llm_history_.size() << ")\n";

    // All tools done; start next round with accumulated history.
    StartRound(llm_history_);
    return;
  }

  const LlmToolCall tc = pending_tool_calls_[pending_tool_call_idx_];
  pending_tool_call_idx_++;

  // Find the function by name.
  auto tools = GetTools();
  std::cerr << "[cpp-agent.tool] lookup function name=" << tc.name << " in tools=" << tools.size() << "\n";
  FunctionPtr fn;
  for (const auto& t : tools) {
    std::cerr << "[cpp-agent.tool]  tool.id=" << t.id << " functions=" << t.functions.size() << "\n";
    for (const auto& f : t.functions) {
      if (!f) continue;
      std::cerr << "[cpp-agent.tool]   candidate fn=" << f->spec().name << "\n";
      if (f->spec().name == tc.name) {
        fn = f;
        break;
      }
    }
    if (fn) break;
  }

  if (!fn) {
    std::cerr << "[cpp-agent.tool] error: unknown tool name=" << tc.name << "\n";
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

  std::cerr << "[cpp-agent.tool] invoke name=" << fn->spec().name << " tool_call_id=" << tool_call_id
            << " args=" << args << "\n";

  fn->InvokeAsync(std::move(args),
                  [this, tool_call_id = std::move(tool_call_id)](std::string out_result_json, std::string out_error) {
    LlmMessage tool_msg;
    tool_msg.role = LlmRole::kTool;

    if (!out_error.empty()) {
      std::cerr << "[cpp-agent.tool] result tool_call_id=" << tool_call_id << " error=" << out_error << "\n";
      tool_msg.tool_result = LlmToolResult{.tool_call_id = tool_call_id,
                                           .content = build_tool_error_json(out_error)};
    } else {
      std::cerr << "[cpp-agent.tool] result tool_call_id=" << tool_call_id
                << " ok bytes=" << out_result_json.size() << "\n";
      tool_msg.tool_result = LlmToolResult{.tool_call_id = tool_call_id,
                                           .content = std::move(out_result_json)};
    }

    llm_history_.push_back(std::move(tool_msg));
    ExecuteNextToolCall();
  });
}

} // namespace agent
