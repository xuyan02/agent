#include "runtime/professional_agent.h"

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

  if (!StartLlmRequest(model_, input, std::move(on_token), std::move(on_done))) {
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
