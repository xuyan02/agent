#include "agent/agent_context.h"
#include "agent/smart/intuitive_agent.h"

#include "dust/message_loop/linux_message_pump_epoll.h"
#include "dust/message_loop/message_loop.h"

#include <cstdio>
#include <memory>

namespace {

class ThinkToolAgentContext final : public agent::AgentContext {
 public:
  std::string GetModelName() const override { return "gpt-4o-mini"; }

  std::string GetSystemPrompt() const override {
    return "You are a test harness.";
  }

};

}  // namespace

int main() {
  auto pump = std::make_unique<dust::LinuxMessagePumpEpoll>();
  dust::MessageLoop loop(std::move(pump));

  ThinkToolAgentContext ctx;
  agent::LlmContext llm_ctx;
  agent::Runtime runtime(&llm_ctx);

  agent::IntuitiveAgent intuitive(&runtime, &ctx);

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
