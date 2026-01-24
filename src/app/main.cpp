#include "app/config.h"
#include "app/wiring.h"

#include "core/agent.h"
#include "core/errors.h"

#include <iostream>

int main(int argc, char** argv) {
  try {
    // Prefer a user-global config by default.
    std::string config_path = "~/.cpp-agent/settings.json";
    if (argc >= 2) config_path = argv[1];

    auto cfg = cpp_agent::app::load_config_or_throw(config_path);
    auto agent = cpp_agent::app::build_agent_or_throw(cfg);
    agent->repl();
    return 0;
  } catch (const cpp_agent::core::AgentError& e) {
    std::cerr << "error: " << e.message() << "\n";
    return 2;
  } catch (const std::exception& e) {
    std::cerr << "error: " << e.what() << "\n";
    return 2;
  }
}
