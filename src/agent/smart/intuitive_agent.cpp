#include "agent/smart/intuitive_agent.h"

#include "agent/smart/smart_agent.h"

#include <memory>
#include <utility>

namespace agent {

IntuitiveAgent::IntuitiveAgent(agent::Runtime* runtime, const agent::SmartAgent* smart)
    : agent::SimpleAgent(runtime), smart_(smart) {}

std::string IntuitiveAgent::GetName() const {
  if (!smart_)
    return {};
  return smart_->GetName();
}

std::string IntuitiveAgent::GetModel() const {
  return "gpt-4o";
}

std::vector<std::string> IntuitiveAgent::GetActiveToolNames() const {
  return {"shallow_think"};
}

std::string IntuitiveAgent::GetAgentPrompt() const {
  if (!runtime())
    return {};
  return runtime()->GetPrompt("intuitive");
}

void IntuitiveAgent::Run(std::string input,
                         dust::OnceFunction<void(std::string answer)> on_done,
                         dust::OnceFunction<void(std::string error)> on_error) {
  agent::SimpleAgent::Run(std::move(input), std::move(on_done), std::move(on_error));
}

}  // namespace agent
