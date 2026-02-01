#pragma once

#include "agent/agent.h"
#include "agent/agent_context.h"

#include <string>

namespace agent {

class IntuitiveAgent final : public Agent {
 public:
  IntuitiveAgent(agent::Runtime* runtime, const agent::AgentContext* base_ctx);

  void Run(std::string input,
           dust::OnceFunction<void(std::string answer)> on_done,
           dust::OnceFunction<void(std::string error)> on_error) override;

 private:
  const agent::AgentContext* base_ctx_{nullptr};
};

}  // namespace agent
