#include "runtime/agent.h"

#include "runtime/runtime.h"
#include "runtime/team.h"

#include <cstdlib>
#include <cstring>
#include <iostream>

namespace agent {
namespace {

bool DebugLlmRequestSummary() {
  const char* v = std::getenv("CPP_AGENT_DEBUG_LLM_VERBOSE");
  return v && *v && std::strcmp(v, "0") != 0;
}

const char* RoleName(LlmRole r) {
  switch (r) {
    case LlmRole::kSystem: return "system";
    case LlmRole::kUser: return "user";
    case LlmRole::kAssistant: return "assistant";
    case LlmRole::kTool: return "tool";
  }
  return "unknown";
}

} // namespace


Agent::Agent(Team& team, std::string name) : team_(team), name_(std::move(name)) {}

Agent::~Agent() = default;

std::string Agent::name() const { return name_; }

Team& Agent::team() { return team_; }

const Team& Agent::team() const { return team_; }

Runtime& Agent::runtime() { return team_.runtime(); }

const Runtime& Agent::runtime() const { return team_.runtime(); }

bool Agent::HasActiveRequest() const { return active_req_ != nullptr; }

std::string Agent::GetSystemPrompt() const { return {}; }

std::vector<Tool> Agent::GetTools() { return {}; }

std::vector<std::string> Agent::GetActiveTools() const { return {}; }

std::vector<std::string> Agent::GetActiveSkills() const { return {}; }

bool Agent::StartLlmRequest(std::string model,
                            std::vector<LlmMessage> messages,
                            agent::LlmRequest::OnToken on_token,
                            agent::LlmRequest::OnToolCalls on_tool_calls,
                            agent::LlmRequest::OnDone on_done) {
  if (active_req_) return false;

  // Debug summary (guarded by CPP_AGENT_DEBUG_LLM).
  if (DebugLlmRequestSummary()) {
    const size_t n = messages.size();
    const char* last_role = n ? RoleName(messages.back().role) : "none";

    bool last_user_is_resume = false;
    if (n && messages.back().role == LlmRole::kUser) {
      last_user_is_resume = (messages.back().content == "[resume]\n");
    }

    const auto tools = GetTools();

    std::cerr << "[cpp-agent.llm] req agent=" << name_ << " model=" << model << " msgs=" << n
              << " last.role=" << last_role << " last.user.resume=" << (last_user_is_resume ? 1 : 0)
              << " tools=" << tools.size() << "\n";

    // Also print a tiny per-role count to understand history composition.
    size_t sys = 0, user = 0, asst = 0, tool = 0;
    for (const auto& m : messages) {
      switch (m.role) {
        case LlmRole::kSystem: sys++; break;
        case LlmRole::kUser: user++; break;
        case LlmRole::kAssistant: asst++; break;
        case LlmRole::kTool: tool++; break;
      }
    }
    std::cerr << "[cpp-agent.llm] req.roles system=" << sys << " user=" << user << " assistant=" << asst
              << " tool=" << tool << "\n";
  }

  active_req_ = runtime().llm().Create(std::move(model),
                                      std::move(messages),
                                      GetTools(),
                                      std::move(on_token),
                                      std::move(on_tool_calls),
                                      std::move(on_done));

  return active_req_ != nullptr;
}

void Agent::CancelActiveRequest() { active_req_.reset(); }

} // namespace agent
