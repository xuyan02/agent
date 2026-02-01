#pragma once

#include "agent/agent_context.h"

#include <string>
#include <vector>

namespace agent {

class PromptOverrideAgentContext final : public agent::AgentContext {
 public:
  PromptOverrideAgentContext(const agent::AgentContext* base,
                             std::string system_prompt,
                             bool tools_enabled);

  std::string GetModelName() const override;
  std::string GetSystemPrompt() const override;
  std::vector<agent::Tool> GetTools() const override;

 private:
  const agent::AgentContext* base_{nullptr};
  std::string system_prompt_;
  bool tools_enabled_{true};
};

}  // namespace agent
