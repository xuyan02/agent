#pragma once

#include "dust/functional/function.h"

#include "agent/agent_context.h"

#include "runtime/runtime.h"
#include "tool/tool.h"
#include "tool/tool_provider.h"

#include <string>
#include <vector>

namespace agent {

class Agent : public agent::ToolProvider {
 public:
  Agent(agent::Runtime* runtime, const agent::AgentContext* ctx);
  virtual ~Agent() = default;

  Agent(const Agent&) = delete;
  Agent& operator=(const Agent&) = delete;

  // Asynchronously execute one "round": given an input text, produce a final answer.
  //
  // Contract:
  // - No concurrent Run() calls on the same instance; busy must call on_error(...).
  // - Exactly one of on_done/on_error must be invoked exactly once.
  // - Callbacks are invoked on the Agent's MessageLoop thread.
  virtual void Run(std::string input,
                   dust::OnceFunction<void(std::string answer)> on_done,
                   dust::OnceFunction<void(std::string error)> on_error) = 0;

 protected:
  agent::Runtime* runtime();
  const agent::Runtime* runtime() const;

  const agent::AgentContext* ctx() const { return ctx_; }

 public:
  void RegisterTool(agent::ToolPtr tool);

  agent::Tool* FindTool(const std::string& tool_id) const override;

 protected:

 private:
  agent::Runtime* runtime_;
  const agent::AgentContext* ctx_{nullptr};
  std::vector<agent::ToolPtr> tools_;
};

}  // namespace agent
