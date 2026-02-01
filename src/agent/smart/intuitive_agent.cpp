#include "agent/smart/intuitive_agent.h"

#include "agent/smart/prompt_overrides.h"
#include "agent/simple_agent.h"

#include <memory>
#include <utility>

namespace agent {

namespace {

std::string BuildIntuitiveSystemPrompt() {
  return R"PROMPT(You are IntuitiveAgent.

Instructions:
- Speak naturally. Do NOT output JSON.
- Do NOT reveal chain-of-thought.
- Reply in the same language as the user.

Tool usage rule:
- If you can answer immediately with high confidence, answer directly.
- Otherwise, call tool function think.shallow_think to think further.

When calling think.shallow_think:
- Provide arguments:
  - thought: a short natural-language summary of what you think is needed (no chain-of-thought)
  - content: the user's request verbatim (or a faithful restatement)

After the tool returns:
- Use the tool output to produce the final user-facing answer.
)PROMPT";
}

}  // namespace

IntuitiveAgent::IntuitiveAgent(agent::Runtime* runtime, const agent::AgentContext* base_ctx)
    : Agent(runtime, base_ctx) {}

void IntuitiveAgent::Run(std::string input,
                         dust::OnceFunction<void(std::string answer)> on_done,
                         dust::OnceFunction<void(std::string error)> on_error) {
  PromptOverrideAgentContext ctx(this->ctx(), BuildIntuitiveSystemPrompt(), /*tools_enabled=*/true);
  auto impl = std::make_shared<agent::SimpleAgent>(runtime(), &ctx);
  impl->Run(
      std::move(input),
      [impl, on_done = std::move(on_done)](std::string answer) mutable {
        if (on_done)
          std::move(on_done)(std::move(answer));
      },
      [impl, on_error = std::move(on_error)](std::string error) mutable {
        if (on_error)
          std::move(on_error)(std::move(error));
      });
}

}  // namespace agent
