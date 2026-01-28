#include "runtime/general_agent.h"

#include "runtime/message_codec.h"
#include "runtime/runtime.h"
#include "runtime/team.h"

#include "runtime/plan2/plan2_functions.h"

#include "dust/message_loop/message_loop.h"

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

    out += "[Skill: ";
    out += s->name;
    out += "]\n";
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
  plan.functions.push_back(std::make_shared<agent::plan2::PlanAddTasksFunction>(&plan2_));
  plan.functions.push_back(std::make_shared<agent::plan2::PlanSetStatusFunction>(&plan2_));
  plan.functions.push_back(std::make_shared<agent::plan2::PlanRemoveTaskFunction>(&plan2_));

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
  if (HasActiveRequest()) return;
  if (!pending_tool_calls_.empty()) return;

  if (queue_.empty()) return;

  // Continue conversation history across batches.
  out_buf_.clear();
  round_mode_ = RoundMode::kUnknown;

  const std::string user_prompt = agent::BuildAgentBatchInput(&queue_);
  const std::string system_prompt = GetSystemPrompt();

  // Ensure a single system message at the beginning.
  if (llm_history_.empty() || llm_history_.front().role != LlmRole::kSystem) {
    llm_history_.insert(llm_history_.begin(), {.role = LlmRole::kSystem, .content = system_prompt});
  } else {
    llm_history_.front().content = system_prompt;
  }

  llm_history_.push_back({.role = LlmRole::kUser, .content = user_prompt});
  TrimHistory();

  StartRound(llm_history_);
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

void GeneralAgent::StartRound(std::vector<LlmMessage> msgs) {
  if (HasActiveRequest()) return;

  std::cerr << "[cpp-agent.llm] StartRound msgs=" << msgs.size();
  if (!msgs.empty()) {
    std::cerr << " last.role="
              << (msgs.back().role == LlmRole::kSystem
                      ? "system"
                      : (msgs.back().role == LlmRole::kUser
                                 ? "user"
                                 : (msgs.back().role == LlmRole::kAssistant ? "assistant" : "tool")));
    if (msgs.back().role == LlmRole::kAssistant) {
      std::cerr << " last.tool_calls=" << msgs.back().tool_calls.size();
    }
    if (msgs.back().role == LlmRole::kTool && msgs.back().tool_result.has_value()) {
      std::cerr << " last.tool_call_id=" << msgs.back().tool_result->tool_call_id;
    }
  }
  std::cerr << "\n";

  round_mode_ = RoundMode::kUnknown;

  auto on_token = [this](std::string tok) {
    if (round_mode_ == RoundMode::kUnknown) {
      round_mode_ = RoundMode::kAssistantText;
      std::cerr << "[cpp-agent.llm] round mode -> assistant_text\n";
    }
    if (round_mode_ != RoundMode::kAssistantText) return;
    OnToken(tok);
  };

  auto on_tool_calls = [this](std::vector<LlmToolCall> tool_calls) {
    if (round_mode_ != RoundMode::kToolCall) {
      round_mode_ = RoundMode::kToolCall;
      std::cerr << "[cpp-agent.llm] round mode -> tool_call\n";
    }
    OnToolCalls(std::move(tool_calls));
  };

  auto on_done = [this]() { OnRequestDone(); };

  std::cerr << "[cpp-agent.llm] StartRound -> StartLlmRequest agent=" << name() << " this=" << this
            << " model=" << model_ << " msgs=" << msgs.size() << "\n";

  if (!StartLlmRequest(model_,
                       std::move(msgs),
                       std::move(on_token),
                       std::move(on_tool_calls),
                       std::move(on_done))) {
    std::cerr << "error: failed to create llm request\n";
    return;
  }
}

void GeneralAgent::OnRequestDone() {
  CancelActiveRequest();

  std::cerr << "[cpp-agent.llm] round done mode="
            << (round_mode_ == RoundMode::kToolCall
                    ? "tool_call"
                    : (round_mode_ == RoundMode::kAssistantText ? "assistant_text" : "unknown"))
            << "\n";

  if (round_mode_ == RoundMode::kToolCall) {
    // Tool execution / next round is driven by OnToolCalls + ExecuteNextToolCall().
    return;
  }

  // Persist final assistant message into conversation history.
  if (!out_buf_.empty()) {
    llm_history_.push_back({.role = LlmRole::kAssistant, .content = out_buf_});
    TrimHistory();
  }

  // Final round: parse once at the end.
  auto out = agent::ParseAgentMultiTargetOutput(name(), out_buf_);
  if (!out_buf_.empty() && out.empty()) {
    std::cerr << "error: agent output dropped (missing @to: header). "
                 "Add '@master:' or '@<agent>:' lines to route messages.\n";
  }

  for (auto& m : out) {
    if (m.to.empty()) {
      std::cerr << "error: empty to in agent output (dropped)\n";
      continue;
    }
    runtime().Emit(m);
  }
  out_buf_.clear();

  // Next tick: try to process any queued messages accumulated while streaming.
  auto* loop = dust::MessageLoop::Current();
  if (!loop) {
    std::cerr << "error: no MessageLoop::Current()\n";
    return;
  }
  loop->task_runner()->PostTask([this]() { TryStartRequest(); });
}


void GeneralAgent::OnToken(const std::string& tok) {
  if (tok.empty()) return;

  // Keep streaming tokens until request completes, then parse once at the end.
  out_buf_ += tok;
}

} // namespace agent
