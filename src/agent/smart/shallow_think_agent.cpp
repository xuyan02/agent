#include "agent/smart/shallow_think_agent.h"

#include "agent/smart/smart_agent.h"

#include <cstdlib>
#include <cstdio>
#include <utility>

namespace agent {

ShallowThinkAgent::ShallowThinkAgent(agent::Runtime* runtime, const agent::SmartAgent* smart)
    : agent::SimpleAgent(runtime), smart_(smart) {}

std::string ShallowThinkAgent::GetName() const {
  if (!smart_)
    return {};
  return smart_->GetName();
}

std::string ShallowThinkAgent::GetModel() const {
  return "gpt-4o";
}

std::string ShallowThinkAgent::GetAgentPrompt() const {
  if (!runtime())
    return {};
  return runtime()->GetPrompt("shallow_think");
}

std::vector<std::string> ShallowThinkAgent::GetActiveToolNames() const {
  return {};
}

void ShallowThinkAgent::Run(std::string input,
                            dust::OnceFunction<void(std::string answer)> on_done,
                            dust::OnceFunction<void(std::string error)> on_error) {
  const bool dbg = std::getenv("CPP_AGENT_DEBUG_SHALLOW_THINK") != nullptr;
  if (dbg) {
    std::fprintf(stderr, "[cpp-agent.shallow_think] prompt\n%.*s\n",
                 static_cast<int>(GetSystemPrompt().size()), GetSystemPrompt().c_str());
    std::fprintf(stderr, "[cpp-agent.shallow_think] input\n%.*s\n",
                 static_cast<int>(input.size()), input.c_str());
  }

  agent::SimpleAgent::Run(
      std::move(input),
      [dbg, on_done = std::move(on_done)](std::string answer) mutable {
        if (dbg) {
          std::fprintf(stderr, "[cpp-agent.shallow_think] output\n%.*s\n",
                       static_cast<int>(answer.size()), answer.c_str());
        }
        if (on_done)
          std::move(on_done)(std::move(answer));
      },
      std::move(on_error));
}

}  // namespace agent
