#pragma once

#include "agent/agent.h"

namespace agent {

class LlmAgent final : public Agent {
 public:
  LlmAgent();
  ~LlmAgent() override;

  LlmAgent(const LlmAgent&) = delete;
  LlmAgent& operator=(const LlmAgent&) = delete;

  dust::FuturePtr<dust::Result<void, std::string>> Run(
      dust::RefPtr<AgentContext> context) override;
};

}  // namespace agent
