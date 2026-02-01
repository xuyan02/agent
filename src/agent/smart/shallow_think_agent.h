#pragma once

#include "agent/simple_agent.h"

#include <string>
#include <vector>

namespace agent {

class SmartAgent;

class ShallowThinkAgent final : public agent::SimpleAgent {
 public:
  ShallowThinkAgent(agent::Runtime* runtime, const agent::SmartAgent* smart);

  std::string GetName() const override;
  std::string GetModel() const override;

  std::string GetAgentPrompt() const override;

  void Run(std::string input,
           dust::OnceFunction<void(std::string answer)> on_done,
           dust::OnceFunction<void(std::string error)> on_error) override;

  std::vector<std::string> GetActiveToolNames() const override;

 private:
  const agent::SmartAgent* smart_{nullptr};
};

}  // namespace agent
