#include "runtime/professional_agent.h"

#include "infra/llm/llm_message.h"
#include "runtime/team.h"

#include <iostream>

namespace agent {

ProfessionalAgent::ProfessionalAgent(Team& team, std::string name, std::string model)
    : Agent(team, std::move(name)), model_(std::move(model)) {}

ProfessionalAgent::~ProfessionalAgent() = default;

void ProfessionalAgent::Input(const std::string& input, ReplyFn reply) {
  if (HasActiveRequest()) {
    std::cerr << "error: professional agent is busy\n";
    if (reply) reply("");
    return;
  }

  out_.clear();
  reply_ = std::move(reply);

  auto on_token = [this](std::string tok) { OnToken(tok); };
  auto on_done = [this]() { OnDone(); };

  std::vector<LlmMessage> msgs;
  msgs.push_back({.role = LlmRole::kSystem, .content = GetSystemPrompt()});
  msgs.push_back({.role = LlmRole::kUser, .content = input});

  if (!StartLlmRequest(model_,
                       std::move(msgs),
                       std::move(on_token),
                       agent::LlmRequest::OnToolCalls{},
                       std::move(on_done))) {
    std::cerr << "error: failed to create llm request\n";
    if (reply_) reply_("");
    reply_ = {};
    return;
  }
}

void ProfessionalAgent::OnToken(const std::string& tok) {
  if (tok.empty()) return;
  out_ += tok;
}

void ProfessionalAgent::OnDone() {
  CancelActiveRequest();

  if (reply_) reply_(out_);
  reply_ = {};
  out_.clear();
}

} // namespace agent
