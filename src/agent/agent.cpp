#include "agent/agent.h"

namespace agent {

Agent::Agent(agent::Runtime* runtime, const agent::AgentContext* ctx)
    : runtime_(runtime), ctx_(ctx) {}

agent::Runtime* Agent::runtime() {
  return runtime_;
}

const agent::Runtime* Agent::runtime() const {
  return runtime_;
}

void Agent::RegisterTool(agent::ToolPtr tool) {
  if (!tool)
    return;
  tool->Init();
  tools_.push_back(std::move(tool));
}

agent::Tool* Agent::FindTool(const std::string& tool_id) const {
  for (const auto& t : tools_) {
    if (t && t->id == tool_id)
      return t.get();
  }
  return runtime_ ? runtime_->FindTool(tool_id) : nullptr;
}

}  // namespace agent
