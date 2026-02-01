#include "agent/smart/intuitive_agent.h"

#include "agent/smart/prompt_overrides.h"
#include "agent/simple_agent.h"

#include <memory>
#include <utility>

namespace agent {

namespace {

std::string BuildIntuitiveSystemPrompt() {
  return R"PROMPT(You are IntuitiveAgent. You must output ONLY one JSON object and nothing else.

Output schema:
{
  "outcome": "answer" | "shallow" | "deep",
  "content": "...",
  "reason": "..."
}

Rules:
- The JSON must be valid.
- Use EXACT keys: outcome, content, reason.
- If outcome is "answer": content MUST be a final user-facing reply (can be a refusal/explanation).
- If outcome is "shallow" or "deep": content MUST be "".
- reason is optional; keep it short.
- NEVER call tools.
- Do NOT include chain-of-thought.
- Reply in the same language as the user.

Routing guidance:
- If you can answer immediately with high confidence, choose outcome "answer".
- If the task needs a small amount of reasoning or light checking, choose outcome "shallow".
- If the task is clearly complex, multi-step, ambiguous, or requires deep reasoning, choose outcome "deep".
)PROMPT";
}

}  // namespace

IntuitiveAgent::IntuitiveAgent(agent::Runtime* runtime, const agent::AgentContext* base_ctx)
    : Agent(runtime, base_ctx) {}

void IntuitiveAgent::Run(std::string input,
                         dust::OnceFunction<void(std::string answer)> on_done,
                         dust::OnceFunction<void(std::string error)> on_error) {
  PromptOverrideAgentContext ctx(this->ctx(), BuildIntuitiveSystemPrompt(), /*tools_enabled=*/false);
  auto impl = std::make_shared<agent::SimpleAgent>(runtime(), &ctx);
  impl->Run(std::move(input),
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
