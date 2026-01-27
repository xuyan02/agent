#include "core/agent.h"

#include "core/tool_log.h"

#include "dust/message_loop/message_loop.h"

#include <sstream>

namespace agent {

Agent::Agent(agent::LlmContext& llm,
             agent::IConsole& console,
             agent::IStorage& storage,
             Policy policy,
             std::unordered_map<std::string, std::unique_ptr<agent::ITool>> tools,
             agent::LlmOptions llm_options,
             agent::PlanStore& plan_store,
             std::string plan_prompt_md)
    : llm_(llm),
      console_(console),
      storage_(storage),
      policy_(std::move(policy)),
      tools_(std::move(tools)),
      llm_options_(std::move(llm_options)),
      plan_store_(plan_store),
      plan_prompt_md_(std::move(plan_prompt_md)) {
  Message sys;
  sys.role = Role::kSystem;
  sys.content = "You are a CLI coding assistant. Prefer concise answers.";
  conv_.add(std::move(sys));
}

void Agent::Repl(dust::MessageLoop& loop) {
  console_.PrintLine("cpp-agent (type /exit to quit)");

  console_.SetOnLine([this, &loop](std::string line) {
    if (line == "/exit") {
      loop.Quit();
      return;
    }

    if (line == "/plan") {
      console_.PrintLine(plan_store_.render_markdown());
      return;
    }

    if (line.empty()) return;
    handle_user_input(line);
  });
}

void Agent::handle_user_input(const std::string& input) {
  auto refresh_system_prompt_from_plan = [&]() {
    if (auto* sys = conv_.first_system_message(); sys) {
      sys->content = "You are a CLI coding assistant. Prefer concise answers.\n\n";
      if (!plan_prompt_md_.empty()) {
        sys->content += plan_prompt_md_;
        if (sys->content.back() != '\n') sys->content.push_back('\n');
        sys->content.push_back('\n');
      }
      sys->content += "Current plan:\n";
      sys->content += plan_store_.render_markdown();
    }
  };

  refresh_system_prompt_from_plan();

  Message user;
  user.role = Role::kUser;
  user.content = input;
  conv_.add(user);

  storage_.AppendLogLine(std::string("user: ") + input);

  // For now, streaming only supports a single assistant response without tool calls.
  bool done = false;

  auto on_token = [this](std::string tok) {
    console_.Print(tok);
    storage_.AppendLogLine(std::string("assistant_token: ") + tok);
  };

  auto on_done = [&]() {
    done = true;
  };

  // NOTE: This uses a minimal prompt path. We'll map full conversation+tools next.
  refresh_system_prompt_from_plan();

  std::string system_prompt;
  if (auto* sys = conv_.first_system_message(); sys) system_prompt = sys->content;

  active_req_ = llm_.Create(llm_options_.model,
                            std::move(system_prompt),
                            input,
                            {},
                            std::move(on_token),
                            std::move(on_done));
  if (!active_req_) {
    console_.PrintLine("error: failed to create llm request");
    return;
  }
}

} // namespace agent
