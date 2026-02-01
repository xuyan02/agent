#include "agent/smart/prompt_overrides.h"

namespace agent {

PromptOverrideAgentContext::PromptOverrideAgentContext(const agent::AgentContext* base,
                                                       std::string system_prompt,
                                                       bool tools_enabled)
    : base_(base), system_prompt_(std::move(system_prompt)), tools_enabled_(tools_enabled) {}

std::string PromptOverrideAgentContext::GetModelName() const {
  return base_ ? base_->GetModelName() : std::string{};
}

std::string PromptOverrideAgentContext::GetSystemPrompt() const {
  return system_prompt_;
}

std::vector<agent::ToolPtr> PromptOverrideAgentContext::GetTools() const {
  if (!base_)
    return {};
  if (!tools_enabled_)
    return {};
  return base_->GetTools();
}

}  // namespace agent
