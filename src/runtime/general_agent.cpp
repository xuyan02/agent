#include "runtime/general_agent.h"

#include "runtime/message_codec.h"
#include "runtime/runtime.h"
#include "runtime/team.h"

#include "runtime/plan2/plan2_functions.h"

#include "dust/message_loop/message_loop.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cctype>
#include <iostream>
#include <utility>

namespace agent {

GeneralAgent::GeneralAgent(Team& team,
                           std::string name,
                           std::string model)
    : Agent(team, std::move(name)), model_(std::move(model)) {}

GeneralAgent::~GeneralAgent() = default;

std::string GeneralAgent::RenderPlanMarkdown() const {
  return plan2_.RenderMarkdown();
}

void GeneralAgent::Input(const Message& msg) {
  queue_.push_back(msg);
  // Avoid re-entrancy when Input() is called while an LLM request is being built.
  if (in_flight_) return;
  TryStartRequest();
}

std::string GeneralAgent::GetSystemPrompt() const {
  std::string out;

  for (const auto& skill_name : GetActiveSkills()) {
    const auto* s = runtime().skills().Find(skill_name);
    if (!s) {
      std::cerr << "error: missing default skill: " << skill_name << "\n";
      continue;
    }

    out += s->prompt_md;
    if (!out.empty() && out.back() != '\n') out.push_back('\n');
    out += "\n---\n\n";
  }

  // Keep plan state visible (the plan skill teaches how to use the tool).
  out += plan2_.RenderMarkdown();
  if (!out.empty() && out.back() != '\n') out.push_back('\n');
  out += "\n---\n\n";

  return out;
}

std::vector<Tool> GeneralAgent::GetTools() {
  Tool plan;
  plan.id = "plan";
  plan.description = "Task planning and management";
  plan.functions.push_back(std::make_shared<agent::plan2::PlanAddTaskFunction>(&plan2_));
  plan.functions.push_back(std::make_shared<agent::plan2::PlanAddSubtaskFunction>(&plan2_));
  plan.functions.push_back(std::make_shared<agent::plan2::PlanActivateTaskFunction>(&plan2_));
  plan.functions.push_back(std::make_shared<agent::plan2::PlanSuspendTaskFunction>(&plan2_));
  plan.functions.push_back(std::make_shared<agent::plan2::PlanResumeTaskFunction>(&plan2_));
  plan.functions.push_back(std::make_shared<agent::plan2::PlanCompleteTaskFunction>(&plan2_));
  plan.functions.push_back(std::make_shared<agent::plan2::PlanAbandonTaskFunction>(&plan2_));
  plan.functions.push_back(std::make_shared<agent::plan2::PlanRemoveTaskFunction>(&plan2_));
  plan.functions.push_back(std::make_shared<agent::plan2::PlanClearSubtasksFunction>(&plan2_));

  return {plan};
}

std::vector<std::string> GeneralAgent::GetActiveSkills() const {
  return {"general_agent", "plan"};
}

std::vector<std::string> GeneralAgent::GetActiveTools() const {
  std::vector<std::string> out;

  for (const auto& skill_name : GetActiveSkills()) {
    const auto* s = runtime().skills().Find(skill_name);
    if (!s) continue;

    for (const auto& t : s->tools) {
      if (t.empty()) continue;
      if (std::find(out.begin(), out.end(), t) != out.end()) continue;
      out.push_back(t);
    }
  }

  return out;
}

void GeneralAgent::TryStartRequest() {
  if (in_flight_) return;

  SendUserBatchRequest();
}

void GeneralAgent::SendUserBatchRequest() {
  in_flight_ = true;

  out_buf_.clear();

  had_tool_calls_ = false;
  pending_tool_call_count_ = 0;

  std::string user_prompt;
  if (queue_.empty()) {
    user_prompt = "[resume]\n";
    if (std::getenv("CPP_AGENT_DEBUG_CONTROL_FRAMES")) {
      std::cerr << "[cpp-agent.control] resume agent=" << name() << "\n";
    }
  } else {
    user_prompt = agent::BuildAgentBatchInput(&queue_);
  }
  const std::string system_prompt = GetSystemPrompt();

  // Ensure a single system message at the beginning.
  if (llm_history_.empty() || llm_history_.front().role != LlmRole::kSystem) {
    llm_history_.insert(llm_history_.begin(), {.role = LlmRole::kSystem, .content = system_prompt});
  } else {
    llm_history_.front().content = system_prompt;
  }

  llm_history_.push_back({.role = LlmRole::kUser, .content = user_prompt});
  TrimHistory();

  auto on_token = [this](std::string tok) { OnToken(tok); };

  auto on_tool_calls = [this](std::vector<LlmToolCall> tool_calls) { OnToolCalls(std::move(tool_calls)); };

  auto on_done = [this]() { OnRequestDone(); };

  if (!StartLlmRequest(model_,
                       llm_history_,
                       std::move(on_token),
                       std::move(on_tool_calls),
                       std::move(on_done))) {
    std::cerr << "error: failed to create llm request\n";
    return;
  }
}

void GeneralAgent::SendToolReplyRequest() {
  out_buf_.clear();

  auto on_token = [this](std::string tok) { OnToken(tok); };

  auto on_tool_calls = [this](std::vector<LlmToolCall> tool_calls) { OnToolCalls(std::move(tool_calls)); };

  auto on_done = [this]() { OnRequestDone(); };

  if (!StartLlmRequest(model_,
                       llm_history_,
                       std::move(on_token),
                       std::move(on_tool_calls),
                       std::move(on_done))) {
    std::cerr << "error: failed to create llm request\n";
    return;
  }
}

void GeneralAgent::TrimHistory() {
  if (llm_history_.size() <= max_history_messages_) return;
  if (llm_history_.empty()) return;

  // Keep the first system message, drop the oldest tail messages.
  const LlmMessage sys = llm_history_.front();
  std::vector<LlmMessage> kept;
  kept.reserve(max_history_messages_);
  kept.push_back(sys);

  const size_t want_tail = max_history_messages_ - 1;
  if (llm_history_.size() > 1) {
    const size_t start = llm_history_.size() - want_tail;
    for (size_t i = start; i < llm_history_.size(); i++) kept.push_back(llm_history_[i]);
  }

  llm_history_ = std::move(kept);
}

void GeneralAgent::OnRequestDone() {
  CancelActiveRequest();

  const bool had_tool_calls = had_tool_calls_;

  if (had_tool_calls) {
    had_tool_calls_ = false;

    if (pending_tool_call_count_ == 0) {
      SendToolReplyRequest();
    }
    return;
  }

  // Final assistant text round.
  in_flight_ = false;

  auto schedule_retry = [&](std::string why) {
    format_retry_count_++;
    if (format_retry_count_ > kMaxFormatRetries) {
      std::cerr << "error: agent output format retry exceeded (" << why << ")\n";
      out_buf_.clear();
      return;
    }

    if (std::getenv("CPP_AGENT_DEBUG_LLM")) {
      std::cerr << "[cpp-agent.llm] format_retry agent=" << name() << " n=" << format_retry_count_
                << " why=" << why << "\n";
    }

    // Inject a correction instruction and retry immediately. This round produces no external output.
    llm_history_.push_back({.role = LlmRole::kSystem,
                            .content =
                                "FORMAT ERROR: Your previous reply did not follow the required output format. "
                                "This message is not a user request. Reply again to the same user request, but in the correct format. "
                                "Allowed formats:\n"
                                "1) Exactly '[pause]' alone (no other text).\n"
                                "2) One or more routed blocks, each starting with '@<target>:' (e.g. '@master: ...'), followed by optional continuation lines.\n"
                                "Disallowed: any other text outside these frames; empty target like '@:'."});
    TrimHistory();

    out_buf_.clear();
    TryStartRequest();
  };

  if (std::getenv("CPP_AGENT_DEBUG_CONTROL_FRAMES")) {
    const bool is_pause = (out_buf_ == "[pause]\n" || out_buf_ == "[pause]");
    if (is_pause) {
      std::cerr << "[cpp-agent.control] pause agent=" << name() << "\n";
    }
  }

  const bool contains_pause = (out_buf_.find("[pause]") != std::string::npos);
  const bool is_pause_only = (out_buf_ == "[pause]\n" || out_buf_ == "[pause]");

  // B) Mixed control frame: contains [pause] but has other content.
  if (contains_pause && !is_pause_only) {
    schedule_retry("mixed_pause");
    return;
  }

  // Control frame: if assistant replies with [pause], stop auto-driving new rounds.
  // [pause] must be the only output (per skill protocol).
  if (is_pause_only) {
    out_buf_.clear();
    format_retry_count_ = 0;
    return;
  }

  // Keep assistant output in history for context. If the output is malformed we will append an
  // additional correction instruction and retry; we still keep the malformed output to show what failed.
  if (!out_buf_.empty()) {
    llm_history_.push_back({.role = LlmRole::kAssistant, .content = out_buf_});
    TrimHistory();
  } else {
    // Empty assistant text round: not a format violation.
    format_retry_count_ = 0;
  }

  auto out = agent::ParseAgentMultiTargetOutput(name(), out_buf_);
  // A) Missing routing header: non-empty output but no parsable @to: blocks.
  if (!out_buf_.empty() && out.empty()) {
    schedule_retry("missing_to_header");
    return;
  }

  bool any_empty_to = false;
  for (auto& m : out) {
    if (m.to.empty()) {
      any_empty_to = true;
      continue;
    }
    runtime().Emit(m);
  }
  // C) Parsed block but empty target.
  if (any_empty_to) {
    schedule_retry("empty_to");
    return;
  }
  out_buf_.clear();
  format_retry_count_ = 0;

  auto* loop = dust::MessageLoop::Current();
  if (loop) {
    loop->task_runner()->PostTask([this]() { TryStartRequest(); });
  } else {
    // Tests may run without a MessageLoop; execute synchronously.
    TryStartRequest();
  }
}


static std::string build_tool_error_json(const std::string& msg) {
  // OpenAI-compatible: tool message content is a string. We embed a JSON object as a string.
  nlohmann::json j;
  j["error"] = msg;
  return j.dump();
}

void GeneralAgent::OnToolCalls(std::vector<LlmToolCall> tool_calls) {
  if (tool_calls.empty()) return;

  had_tool_calls_ = true;

  if (std::getenv("CPP_AGENT_DEBUG_LLM")) {
    std::cerr << "[cpp-agent.llm] resp(tool_calls) agent=" << name() << " n=" << tool_calls.size() << "\n";
    for (size_t i = 0; i < tool_calls.size(); i++) {
      const auto& tc = tool_calls[i];
      std::cerr << "[cpp-agent.llm]  tc[" << i << "] id=" << tc.id << " name=" << tc.name
                << " args.len=" << tc.arguments_json.size() << "\n";
    }
  }

  LlmMessage assistant;
  assistant.role = LlmRole::kAssistant;
  assistant.tool_calls = tool_calls;
  llm_history_.push_back(std::move(assistant));

  ExecuteToolCalls(std::move(tool_calls));
}

void GeneralAgent::ExecuteToolCalls(std::vector<LlmToolCall> tool_calls) {
  pending_tool_call_count_ += tool_calls.size();

  // Find all tools on demand (simple and stable; list is tiny).
  const auto tools = GetTools();

  for (const auto& tc : tool_calls) {
    FunctionPtr fn;
    for (const auto& t : tools) {
      fn = t.FindFunctionByName(tc.name);
      if (fn) break;
    }

    if (!fn) {
      LlmMessage tool_msg;
      tool_msg.role = LlmRole::kTool;
      tool_msg.tool_result = LlmToolResult{.tool_call_id = tc.id,
                                           .content = build_tool_error_json("Unknown tool: " + tc.name)};
      llm_history_.push_back(std::move(tool_msg));

      pending_tool_call_count_--;
      if (pending_tool_call_count_ == 0 && had_tool_calls_ == false) {
        SendToolReplyRequest();
      }
      continue;
    }

    std::string args = tc.arguments_json;
    std::string tool_call_id = tc.id;

    fn->InvokeAsync(std::move(args),
                    [this, tool_call_id = std::move(tool_call_id)](std::string out_result_json,
                                                                  std::string out_error) {
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

      pending_tool_call_count_--;
      if (pending_tool_call_count_ == 0 && had_tool_calls_ == false) {
        SendToolReplyRequest();
      }
    });
  }
}

void GeneralAgent::OnToken(const std::string& tok) {
  if (tok.empty()) return;
  out_buf_ += tok;
}

} // namespace agent
