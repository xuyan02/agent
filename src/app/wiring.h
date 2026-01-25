#pragma once

#include "app/config.h"

#include "interfaces/iconsole.h"

#include <memory>

namespace cpp_agent::core {
class Agent;
}

namespace cpp_agent::app {

std::unique_ptr<cpp_agent::core::Agent> build_agent_or_throw(const AppConfig& cfg,
                                                             cpp_agent::interfaces::IConsole& console);

} // namespace cpp_agent::app
