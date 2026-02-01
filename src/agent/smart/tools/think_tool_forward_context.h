#pragma once

#include "agent/agent_context.h"

#include <utility>

namespace agent {

class ToolAppendAgentContext final : public agent::AgentContext {
 public:
  ToolAppendAgentContext(const agent::AgentContext* base, std::vector<agent::ToolPtr> extra)
      : base_(base), extra_(std::move(extra)) {}

  std::string GetModelName() const override { return base_ ? base_->GetModelName() : std::string{}; }
  std::string GetSystemPrompt() const override { return base_ ? base_->GetSystemPrompt() : std::string{}; }

  std::vector<agent::ToolPtr> GetTools() const override {
    std::vector<agent::ToolPtr> out;
    if (base_) {
      out = base_->GetTools();
    }
    for (auto& t : extra_) {
      out.push_back(std::move(t));
    }
    extra_.clear();
    return out;
  }

 private:
  const agent::AgentContext* base_{nullptr};
  mutable std::vector<agent::ToolPtr> extra_;
};

}  // namespace agent
