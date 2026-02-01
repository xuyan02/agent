#include "agent/agent_context.h"
#include "agent/one_step_agent.h"

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

  if (argc >= 2) {
    cfg_path = argv[1];
  }
  if (argc >= 3) {
    model_name = argv[2];
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
      "You are an assistant that first determines whether the user's request is doable.\n"
      "Possible outcomes: doable / not_doable / needs_more_thought.\n"
      "Rules:\n"
      "- Always start with a line beginning with [reason] explaining your judgment.\n"
      "- You must judge based on your actual capabilities (your tools and other agents you can "
      "use). Do not claim you can do something if you cannot.\n"
      "- If the user's request is ambiguous or requires clarification (goal unclear, missing "
      "constraints), treat it as needs_more_thought.\n"
      "- If the user input is a greeting (e.g. \"hi\", \"hello\"), treat it as doable.\n"
      "- If the user input is meaningless / gibberish, treat it as not_doable.\n"
      "- If outcome is doable or not_doable: then directly provide the final response, starting "
      "with [answer].\n"
      "- If outcome is needs_more_thought: output [thinking] (and do not provide an answer yet).\n";

  SimpleAgentContext ctx(model_name, system_prompt);
  agent::OneStepAgent one_step(&runtime, &ctx);

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

  std::cout << "abc" << std::endl;
  std::string user_input;
  std::cin >> user_input;
  std::cout << "elf" << std::endl;
  if (user_input.empty()) {
    std::cerr << "no input on stdin" << std::endl;
    return 2;
  }

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
