#pragma once

#include "agent/agent.h"
#include "agent/agent_context.h"

#include "infra/llm/llm_message.h"

#include <memory>
#include <string>

namespace agent {

class SimpleAgent final : public Agent {
 public:
  SimpleAgent(agent::Runtime* runtime, const agent::AgentContext* ctx);

  void Run(std::string input,
           dust::OnceFunction<void(std::string answer)> on_done,
           dust::OnceFunction<void(std::string error)> on_error) override;

 private:
  const agent::AgentContext* ctx_{nullptr};

  // Current in-flight callbacks for this agent run.
  dust::OnceFunction<void(std::string answer)> pending_on_done_;
  dust::OnceFunction<void(std::string error)> pending_on_error_;

  void StartRequest();

  void OnToolCalls(std::vector<agent::LlmToolCall> tool_calls);

  void ExecuteToolCalls(size_t index, std::vector<agent::LlmToolCall> tool_calls);

  std::vector<agent::LlmMessage> history_;

  bool busy_{false};
  std::unique_ptr<agent::LlmRequest> req_;
  agent::LlmMessage assistant_msg_;
};

}  // namespace agent
