#include "console/cli_console.h"

#include "agent/agent.h"
#include "runtime/runtime.h"

#include "dust/message_loop/linux_message_pump_epoll.h"
#include "dust/message_loop/message_loop.h"

#include <cstdlib>
#include <iostream>
#include <memory>
#include <string>

namespace {

constexpr const char* kUsage =
    "agent_cli [--root <root_path>]\n"
    "\n"
    "Runs an interactive agent runtime.\n"
    "- Loads config from <root_path>/runtime.json\n"
    "- Loads prompts from <root_path>/prompts/*.md\n";

}  // namespace

int main(int argc, char** argv) {
  std::string root_path;

  for (int i = 1; i < argc; i++) {
    const std::string arg = argv[i];
    if (arg == "--help" || arg == "-h") {
      std::cout << kUsage;
      return 0;
    }
    if (arg == "--root") {
      if (i + 1 >= argc) {
        std::cerr << "error: --root requires a value\n";
        return 2;
      }
      root_path = argv[++i];
      continue;
    }

    std::cerr << "error: unknown arg: " << arg << "\n";
    std::cerr << kUsage;
    return 2;
  }

  auto pump = std::make_unique<dust::LinuxMessagePumpEpoll>();
  dust::MessageLoop loop(std::move(pump));

  agent::CliConsole console;
  console.AttachToMessageLoop(loop);

  agent::Runtime runtime(console, root_path);
  if (!runtime.Init()) {
    console.PrintLine("error: failed to init runtime");
    console.PrintLine("hint: ensure <root>/runtime.json exists and providers are configured");
    return 1;
  }

  agent::Agent* main_agent = runtime.GetMainAgent();
  if (!main_agent) {
    console.PrintLine("error: runtime has no main agent");
    return 1;
  }

  console.PrintLine("ready. type your message and press Enter. type /exit to quit.");

  console.SetOnLine([&](std::string line) {
    if (line == "/exit" || line == "/quit") {
      loop.Quit();
      return;
    }
    if (line.empty())
      return;

    main_agent->Run(
        std::move(line),
        [&](std::string answer) {
          console.PrintLine(answer);
        },
        [&](std::string error) {
          console.PrintLine("error: " + error);
        });
  });

  loop.Run();
  return 0;
}
