#include "agent/smart/shallow_think_agent.h"

#include "agent/smart/smart_agent.h"

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
  agent::SimpleAgent::Run(std::move(input), std::move(on_done), std::move(on_error));
}

}  // namespace agent
