#pragma once

#include "agent/agent.h"
#include "agent/agent_context.h"

#include <string>

namespace agent {

class SmartAgent final : public Agent {
 public:
  SmartAgent(agent::Runtime* runtime, const agent::AgentContext* ctx);

  void Run(std::string input,
           dust::OnceFunction<void(std::string answer)> on_done,
           dust::OnceFunction<void(std::string error)> on_error) override;

 private:
  bool busy_{false};

  dust::OnceFunction<void(std::string answer)> pending_on_done_;
  dust::OnceFunction<void(std::string error)> pending_on_error_;
};

}  // namespace agent
