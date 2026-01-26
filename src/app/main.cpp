#include "app/config.h"
#include "app/wiring.h"

#include "dust/message_loop/linux_message_pump_epoll.h"
#include "dust/message_loop/message_loop.h"

#include "core/agent.h"
#include "infra/console/cli_console.h"

#include <iostream>

int main(int argc, char** argv) {
  // Prefer a user-global config by default.
  std::string config_path = "~/.cpp-agent/settings.json";
  if (argc >= 2) config_path = argv[1];

  auto cfg_r = cpp_agent::app::load_config(config_path);

  // Single-threaded event loop.
  dust::MessageLoop loop(std::make_unique<dust::LinuxMessagePumpEpoll>());

  // CLI console stdin watcher.
  cpp_agent::infra::console::CliConsole console;
  console.AttachToMessageLoop(loop);

  if (!cfg_r.ok()) {
    std::cerr << "config error: " << cfg_r.status().message << "\n";
    return 2;
  }

  auto agent_r = cpp_agent::app::build_agent(cfg_r.value(), console);
  if (!agent_r.ok()) {
    std::cerr << "agent init error: " << agent_r.status().message << "\n";
    return 3;
  }

  // Ensure Agent outlives the message loop callbacks it registers.
  auto agent = std::move(agent_r.value());
  agent->Repl(loop);

  loop.Run();
  return 0;
}
