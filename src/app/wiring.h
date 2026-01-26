#pragma once

#include "app/config.h"

#include "core/status.h"
#include "interfaces/iconsole.h"

#include <memory>

namespace cpp_agent::core {
class Agent;
}

namespace cpp_agent::app {

cpp_agent::core::Result<std::unique_ptr<cpp_agent::core::Agent>> build_agent(const AppConfig& cfg,
                                                                             cpp_agent::interfaces::IConsole& console);

} // namespace cpp_agent::app
