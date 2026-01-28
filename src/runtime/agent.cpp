#include "runtime/agent.h"

#include "runtime/runtime.h"
#include "runtime/team.h"

#include <iostream>

namespace agent {

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

  std::cerr << "[cpp-agent.llm] StartLlmRequest Create agent=" << name_ << " this=" << this
            << " model=" << model << " msgs=" << messages.size() << " tools=" << GetTools().size() << "\n";

  active_req_ = runtime().llm().Create(std::move(model),
                                      std::move(messages),
                                      GetTools(),
                                      std::move(on_token),
                                      std::move(on_tool_calls),
                                      std::move(on_done));

  std::cerr << "[cpp-agent.llm] StartLlmRequest Create done agent=" << name_ << " this=" << this
            << " ok=" << (active_req_ ? 1 : 0) << "\n";
  return active_req_ != nullptr;
}

void Agent::CancelActiveRequest() { active_req_.reset(); }

} // namespace agent
