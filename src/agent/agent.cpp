#include "agent/agent.h"

namespace agent {

Agent::Agent(agent::Runtime* runtime) : runtime_(runtime) {}

agent::Runtime* Agent::runtime() {
  return runtime_;
}

const agent::Runtime* Agent::runtime() const {
  return runtime_;
}

}  // namespace agent
