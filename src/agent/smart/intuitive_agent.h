#pragma once

#include "agent/simple_agent.h"

#include <string>
#include <vector>

namespace agent {

class IntuitiveAgent final : public agent::SimpleAgent {
 public:
  IntuitiveAgent(agent::Runtime* runtime, const agent::AgentContext* base_ctx);

  void Run(std::string input,
           dust::OnceFunction<void(std::string answer)> on_done,
           dust::OnceFunction<void(std::string error)> on_error) override;

  std::vector<std::string> GetActiveToolNames() const override;
};

}  // namespace agent
