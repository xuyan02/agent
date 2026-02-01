#include "agent/agent_context.h"
#include "agent/smart_agent.h"

#include "dust/message_loop/linux_message_pump_epoll.h"
#include "dust/message_loop/message_loop.h"

#include "infra/llm/llm_config_loader.h"
#include "infra/llm/openai_provider_factory.h"

#include <filesystem>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

namespace {

class SimpleAgentContext final : public agent::AgentContext {
 public:
  SimpleAgentContext(std::string model_name, std::string system_prompt)
      : model_name_(std::move(model_name)), system_prompt_(std::move(system_prompt)) {}

  std::string GetModelName() const override { return model_name_; }
  std::string GetSystemPrompt() const override { return system_prompt_; }
  std::vector<agent::Tool> GetTools() const override { return {}; }

 private:
  std::string model_name_;
  std::string system_prompt_;
};

}  // namespace

int main(int argc, char** argv) {
  std::filesystem::path cfg_path = "config/llm_providers.json";
  std::string model_name;
  std::string user_input_arg;

  if (argc >= 2) {
    cfg_path = argv[1];
  }
  if (argc >= 3) {
    model_name = argv[2];
  }
  if (argc >= 4) {
    user_input_arg = argv[3];
  }

  agent::LlmContext llm;
  llm.RegisterFactory(std::make_unique<agent::OpenAIProviderFactory>());

  std::cerr << "config: " << cfg_path << std::endl;

  if (!agent::RegisterProvidersFromConfig(llm, cfg_path)) {
    std::cerr << "failed to load providers from: " << cfg_path << std::endl;
    std::cerr << "hint: copy config/llm_providers.example.json to config/llm_providers.json and "
                 "set env vars"
              << std::endl;
    return 2;
  }

  if (model_name.empty()) {
    // Keep consistent with the original examples: user passes model explicitly.
    model_name = "gpt-4o-mini";
  }

  std::cerr << "model: " << model_name << std::endl;

  agent::Runtime runtime(&llm);

  const std::string system_prompt =
      "You are an assistant.\n";

  SimpleAgentContext ctx(model_name, system_prompt);
  agent::SmartAgent one_step(&runtime, &ctx);

  dust::MessageLoop loop(std::make_unique<dust::LinuxMessagePumpEpoll>());

  int exit_code = 1;
  bool finished = false;

  auto runner = loop.task_runner();
  runner->PostDelayedTask(dust::Duration::FromSeconds(30), dust::OnceClosure([&]() {
                            if (finished)
                              return;
                            std::cerr << "timeout: no response in 30s" << std::endl;
                            exit_code = 124;
                            finished = true;
                            loop.Quit();
                          }));

  if (user_input_arg.empty()) {
    std::cerr << "usage: test_one_step_agent [config_path] [model_name] [user_input]" << std::endl;
    return 2;
  }

  std::string user_input = std::move(user_input_arg);

  one_step.Run(
      std::move(user_input), dust::OnceFunction<void(std::string)>([&](std::string answer) {
        if (finished)
          return;
        finished = true;
        std::cerr << "[cpp-agent.app] on_done called answer.len=" << answer.size() << std::endl;
        std::cout << answer << std::endl;
        exit_code = 0;
        loop.Quit();
      }),
      dust::OnceFunction<void(std::string)>([&](std::string error) {
        if (finished)
          return;
        finished = true;
        std::cerr << "[cpp-agent.app] on_error called error.len=" << error.size() << std::endl;
        std::cerr << "error: " << error << std::endl;
        exit_code = 4;
        loop.Quit();
      }));

  loop.Run();
  return exit_code;
}
