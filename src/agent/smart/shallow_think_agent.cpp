#include "agent/smart/shallow_think_agent.h"

#include "agent/smart/prompt_overrides.h"
#include "agent/simple_agent.h"

#include <memory>
#include <utility>

namespace agent {

namespace {

std::string BuildShallowSystemPrompt() {
  return R"PROMPT(You are ShallowThinkAgent. You must output ONLY one JSON object and nothing else.

Output schema:
{
  "outcome": "answer" | "deep",
  "content": "...",
  "reason": "..."
}

Rules:
- The JSON must be valid.
- Use EXACT keys: outcome, content, reason.
- If outcome is "answer": content MUST be a final user-facing reply (can be a refusal/explanation).
- If outcome is "deep": content MUST be "".
- reason is optional; keep it short.
- Tool usage is allowed but keep it minimal.
- Do NOT include chain-of-thought.
- Reply in the same language as the user.

Routing guidance:
- If you can complete with shallow reasoning and minimal steps, choose outcome "answer".
- If the task requires deeper reasoning / planning / extensive tool usage, choose outcome "deep".
)PROMPT";
}

}  // namespace

ShallowThinkAgent::ShallowThinkAgent(agent::Runtime* runtime, const agent::AgentContext* base_ctx)
    : Agent(runtime, base_ctx) {}

void ShallowThinkAgent::Run(std::string input,
                            dust::OnceFunction<void(std::string answer)> on_done,
                            dust::OnceFunction<void(std::string error)> on_error) {
  PromptOverrideAgentContext ctx(this->ctx(), BuildShallowSystemPrompt(), /*tools_enabled=*/true);
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
