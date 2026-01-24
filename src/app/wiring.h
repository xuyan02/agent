#pragma once

#include "app/config.h"

#include <memory>

namespace cpp_agent::core {
class Agent;
}

namespace cpp_agent::app {

std::unique_ptr<cpp_agent::core::Agent> build_agent_or_throw(const AppConfig& cfg);

} // namespace cpp_agent::app
