#include "agent/smart/shallow_think_agent.h"

#include "agent/smart/prompt_overrides.h"

#include <utility>

namespace agent {

namespace {

std::string BuildShallowSystemPrompt() {
  return R"PROMPT(You are ShallowThinkAgent.

Task:
- Decide whether you can answer the user directly with shallow reasoning.
- If you can: answer the user naturally.

Rules:
- Do NOT include chain-of-thought.
- Do NOT output JSON.
- Reply in the same language as the user.
)PROMPT";
}

}  // namespace

ShallowThinkAgent::ShallowThinkAgent(agent::Runtime* runtime, const agent::AgentContext* base_ctx)
    : agent::SimpleAgent(runtime, base_ctx) {}

std::vector<std::string> ShallowThinkAgent::GetActiveToolNames() const {
  return {};
}

void ShallowThinkAgent::Run(std::string input,
                            dust::OnceFunction<void(std::string answer)> on_done,
                            dust::OnceFunction<void(std::string error)> on_error) {
  PromptOverrideAgentContext ctx(this->ctx(), BuildShallowSystemPrompt(), /*tools_enabled=*/false);

  const agent::AgentContext* prev = ctx_;
  ctx_ = &ctx;
  agent::SimpleAgent::Run(std::move(input), std::move(on_done), std::move(on_error));
  ctx_ = prev;
}

}  // namespace agent
