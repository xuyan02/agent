#include "runtime/general_agent.h"

#include "runtime/message_codec.h"
#include "runtime/runtime.h"
#include "runtime/team.h"

#include "dust/message_loop/message_loop.h"

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

void GeneralAgent::TryStartRequest() {
  if (HasActiveRequest()) return;
  if (queue_.empty()) return;

  const std::string prompt = agent::BuildAgentBatchInput(&queue_);

  auto on_token = [this](std::string tok) { OnToken(tok); };

  auto on_done = [this]() { OnRequestDone(); };

  if (!StartLlmRequest(model_, prompt, std::move(on_token), std::move(on_done))) {
    std::cerr << "error: failed to create llm request\n";
    return;
  }
}

void GeneralAgent::OnRequestDone() {
  CancelActiveRequest();

  for (auto& m : agent::ParseAgentMultiTargetOutput(name(), out_buf_)) {
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
