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

void GeneralAgent::Input(const Message& msg) {
  queue_.push_back(msg);
  TryStartRequest();
}

std::string GeneralAgent::GetSystemPrompt() const {
  std::string out;

  const char* default_skills[] = {"general_agent", "plan"};
  for (const char* skill_name : default_skills) {
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
  Tool t;
  t.id = "plan";
  t.description = "Task planning and management";
  t.functions.push_back(std::make_shared<agent::plan2::PlanAddTasksFunction>(&plan2_));
  t.functions.push_back(std::make_shared<agent::plan2::PlanSetStatusFunction>(&plan2_));
  t.functions.push_back(std::make_shared<agent::plan2::PlanRemoveTaskFunction>(&plan2_));
  return {t};
}

std::vector<std::string> GeneralAgent::GetActiveTools() const {
  std::vector<std::string> out;

  const char* default_skills[] = {"general_agent", "plan"};
  for (const char* skill_name : default_skills) {
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
  if (queue_.empty()) return;

  const std::string user_prompt = agent::BuildAgentBatchInput(&queue_);
  const std::string system_prompt = GetSystemPrompt();

  auto on_token = [this](std::string tok) { OnToken(tok); };

  auto on_done = [this]() { OnRequestDone(); };

  if (!StartLlmRequest(model_, system_prompt, user_prompt, std::move(on_token), std::move(on_done))) {
    std::cerr << "error: failed to create llm request\n";
    return;
  }
}

void GeneralAgent::OnRequestDone() {
  CancelActiveRequest();

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
