#include "runtime/agent.h"

#include "runtime/runtime.h"

namespace agent {

Agent::Agent(Runtime& runtime, std::string name) : runtime_(runtime), name_(std::move(name)) {}

Agent::~Agent() = default;

std::string Agent::name() const { return name_; }

Runtime& Agent::runtime() { return runtime_; }

const Runtime& Agent::runtime() const { return runtime_; }

} // namespace agent
