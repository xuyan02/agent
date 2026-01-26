#pragma once

#include "core/conversation.h"
#include "core/policy.h"
#include "infra/llm/llm_context.h"
#include "infra/plan/plan_store.h"
#include "dust/message_loop/message_loop.h"

#include "interfaces/iconsole.h"
#include "interfaces/illm_client.h"
#include "interfaces/istorage.h"
#include "interfaces/itool.h"

#include <memory>
#include <unordered_map>

namespace agent {

class Agent final {
public:
  Agent(agent::LlmContext& llm,
        agent::IConsole& console,
        agent::IStorage& storage,
        Policy policy,
        std::unordered_map<std::string, std::unique_ptr<agent::ITool>> tools,
        agent::LlmOptions llm_options,
        agent::PlanStore& plan_store,
        std::string plan_prompt_md);

  void Repl(dust::MessageLoop& loop);

private:
  void handle_user_input(const std::string& input);

  agent::LlmContext& llm_;
  std::unique_ptr<agent::LlmRequest> active_req_;
  agent::IConsole& console_;
  agent::IStorage& storage_;
  Policy policy_;
  std::unordered_map<std::string, std::unique_ptr<agent::ITool>> tools_;
  agent::LlmOptions llm_options_;
  agent::PlanStore& plan_store_;
  std::string plan_prompt_md_;

  Conversation conv_;
};

} // namespace agent
