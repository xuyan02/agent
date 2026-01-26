#pragma once

#include "runtime/message.h"

#include "infra/llm/llm_context.h"

#include <memory>
#include <string>

namespace agent {
class IConsole;
}

namespace agent {

class Team;

class Runtime {
public:
  Runtime(agent::IConsole& console, std::unique_ptr<agent::LlmContext> llm);

  agent::LlmContext& llm();
  const agent::LlmContext& llm() const;

  void SetTeam(std::unique_ptr<Team> team);

  void OnCliLine(const std::string& line);

  void Emit(const Message& msg);

private:
  void DeliverToAgent(const Message& msg);

  agent::IConsole& console_;
  std::unique_ptr<agent::LlmContext> llm_;
  std::unique_ptr<Team> team_;
};

} // namespace agent
