#include "agent/smart/intuitive_agent.h"

#include "dust/message_loop/linux_message_pump_epoll.h"
#include "dust/message_loop/message_loop.h"

#include <cstdio>
#include <memory>


int main() {
  auto pump = std::make_unique<dust::LinuxMessagePumpEpoll>();
  dust::MessageLoop loop(std::move(pump));
  agent::Runtime runtime("");
  runtime.Init();

  agent::IntuitiveAgent intuitive(&runtime, /*smart=*/nullptr);

  intuitive.Run(
      "Please briefly explain what a mutex is in C++. If you cannot, call shallow_think.think.",
      [](std::string out) {
        std::fprintf(stderr, "IntuitiveAgent output:\n%s\n", out.c_str());
        dust::MessageLoop::Current()->Quit();
      },
      [](std::string error) {
        std::fprintf(stderr, "error: %s\n", error.c_str());
        dust::MessageLoop::Current()->Quit();
      });

  loop.Run();
  return 0;
}
