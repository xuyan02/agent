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

  auto cfg = cpp_agent::app::load_config_or_throw(config_path);

  // Single-threaded event loop.
  dust::MessageLoop loop(std::make_unique<dust::LinuxMessagePumpEpoll>());

  // CLI console stdin watcher.
  cpp_agent::infra::console::CliConsole console;
  console.AttachToMessageLoop(loop);

  auto agent = cpp_agent::app::build_agent_or_throw(cfg, console);
  agent->Repl(loop);

  loop.Run();
  return 0;
}
