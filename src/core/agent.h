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

namespace cpp_agent::core {

class Agent final {
public:
  Agent(cpp_agent::infra::llm::LlmContext& llm,
        interfaces::IConsole& console,
        interfaces::IStorage& storage,
        Policy policy,
        std::unordered_map<std::string, std::unique_ptr<interfaces::ITool>> tools,
        interfaces::LlmOptions llm_options,
        cpp_agent::infra::plan::PlanStore& plan_store,
        std::string plan_prompt_md);

  void Repl(dust::MessageLoop& loop);

private:
  void handle_user_input(const std::string& input);

  cpp_agent::infra::llm::LlmContext& llm_;
  std::unique_ptr<cpp_agent::infra::llm::LlmRequest> active_req_;
  interfaces::IConsole& console_;
  interfaces::IStorage& storage_;
  Policy policy_;
  std::unordered_map<std::string, std::unique_ptr<interfaces::ITool>> tools_;
  interfaces::LlmOptions llm_options_;
  cpp_agent::infra::plan::PlanStore& plan_store_;
  std::string plan_prompt_md_;

  Conversation conv_;
};

} // namespace cpp_agent::core
