#pragma once

#include "runtime/message.h"

#include <memory>
#include <string>

namespace agent {
class IConsole;
}

namespace agent {

class Team;

class Runtime {
public:
  explicit Runtime(agent::IConsole& console);

  void SetTeam(std::unique_ptr<Team> team);

  void OnCliLine(const std::string& line);

  void Emit(const Message& msg);

private:
  void DeliverToAgent(const Message& msg);

  agent::IConsole& console_;
  std::unique_ptr<Team> team_;
};

} // namespace agent
