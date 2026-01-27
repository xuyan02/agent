#include "runtime/agent.h"

#include "runtime/runtime.h"
#include "runtime/team.h"

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

bool Agent::StartLlmRequest(std::string model,
                            std::string system_prompt,
                            std::string user_prompt,
                            agent::LlmRequest::OnToken on_token,
                            agent::LlmRequest::OnDone on_done) {
  if (active_req_) return false;

  active_req_ = runtime().llm().Create(std::move(model),
                                      std::move(system_prompt),
                                      std::move(user_prompt),
                                      GetTools(),
                                      std::move(on_token),
                                      std::move(on_done));
  return active_req_ != nullptr;
}

void Agent::CancelActiveRequest() { active_req_.reset(); }

} // namespace agent
