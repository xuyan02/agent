#pragma once

#include <memory>
#include <string>

#include <nlohmann/json.hpp>

namespace agent {

class Agent;
class Runtime;

class AgentFactory {
 public:
  virtual ~AgentFactory() = default;

  virtual std::unique_ptr<agent::Agent> Create(agent::Runtime* runtime,
                                              std::string name,
                                              const nlohmann::json& params) const = 0;

  virtual const char* type() const = 0;
};

}  // namespace agent
