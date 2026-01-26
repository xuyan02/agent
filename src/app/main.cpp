#include "app/config.h"
#include "app/wiring.h"

#include "dust/message_loop/linux_message_pump_epoll.h"
#include "dust/message_loop/message_loop.h"

#include "infra/console/cli_console.h"
#include "runtime/runtime.h"
#include "runtime/team.h"

#include <iostream>

int main(int argc, char** argv) {
  // Prefer a user-global config by default.
  std::string config_path = "~/.cpp-agent/settings.json";
  if (argc >= 2) config_path = argv[1];

  auto* cfg = agent::load_config(config_path);

  // Single-threaded event loop.
  dust::MessageLoop loop(std::make_unique<dust::LinuxMessagePumpEpoll>());

  // CLI console stdin watcher.
  agent::CliConsole console;
  console.AttachToMessageLoop(loop);

  if (!cfg) {
    std::cerr << "config error\n";
    return 2;
  }

  // Build LLM context and register providers.
  auto llm = agent::build_llm(*cfg);
  if (!llm) {
    std::cerr << "llm init error\n";
    return 3;
  }

  agent::Runtime runtime(console, std::move(llm));

  // Prototype: load team from repo config.
  auto team = agent::Team::Load(runtime,
                                (cfg->project_root / "config" / "team.json").string(),
                                cfg->llm.model);
  if (!team) {
    std::cerr << "team init error\n";
    return 4;
  }

  runtime.SetTeam(std::move(team));

  console.PrintLine("cpp-agent (type /exit to quit)");
  console.SetOnLine([&](std::string line) {
    if (line == "/exit") {
      loop.Quit();
      return;
    }
    runtime.OnCliLine(line);
  });

  loop.Run();
  return 0;
}
