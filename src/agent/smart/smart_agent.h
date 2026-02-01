#pragma once

#include "agent/agent.h"
#include "agent/agent_context.h"
#include "agent/smart/intuitive_agent.h"
#include "agent/smart/shallow_think_agent.h"

#include <memory>
#include <string>

namespace agent {

class SmartAgent final : public Agent {
 public:
  SmartAgent(agent::Runtime* runtime, const agent::AgentContext* ctx);
  ~SmartAgent() override;

  void Run(std::string input,
           dust::OnceFunction<void(std::string answer)> on_done,
           dust::OnceFunction<void(std::string error)> on_error) override;

  void RunShallowThink(std::string thought,
                      std::string content,
                      dust::OnceFunction<void(std::string answer)> on_done,
                      dust::OnceFunction<void(std::string error)> on_error);

 private:
  bool busy_{false};

  std::unique_ptr<agent::IntuitiveAgent> intuitive_;
  std::unique_ptr<agent::ShallowThinkAgent> shallow_;

  dust::OnceFunction<void(std::string answer)> pending_on_done_;
  dust::OnceFunction<void(std::string error)> pending_on_error_;
};

}  // namespace agent
