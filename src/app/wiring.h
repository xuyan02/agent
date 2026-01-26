#pragma once

#include "app/config.h"

#include "infra/llm/llm_context.h"
#include "interfaces/iconsole.h"

#include <functional>
#include <memory>

namespace agent {
class Agent;
}

namespace agent {

// Legacy single-agent wiring (will be removed once Runtime/Team is fully integrated).
std::unique_ptr<agent::Agent> build_agent(const AppConfig& cfg, agent::IConsole& console);

// Builds and initializes the shared LLM context.
agent::LlmContext* build_llm(const AppConfig& cfg);

} // namespace agent
