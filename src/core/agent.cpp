#include "core/agent.h"

#include "core/errors.h"
#include "core/tool_log.h"

#include <sstream>

namespace cpp_agent::core {

Agent::Agent(interfaces::ILlmClient& llm,
             interfaces::IConsole& console,
             interfaces::IStorage& storage,
             Policy policy,
             std::unordered_map<std::string, std::unique_ptr<interfaces::ITool>> tools,
             interfaces::LlmOptions llm_options)
    : llm_(llm),
      console_(console),
      storage_(storage),
      policy_(std::move(policy)),
      tools_(std::move(tools)),
      llm_options_(std::move(llm_options)) {
  Message sys;
  sys.role = Role::kSystem;
  sys.content = "You are a CLI coding assistant. Prefer concise answers.";
  conv_.add(std::move(sys));
}

void Agent::repl() {
  console_.print_line("cpp-agent (type /exit to quit)");
  for (;;) {
    auto line = console_.read_line("> ");
    if (!line) break;
    if (*line == "/exit") break;

    if (*line == "/plan") {
      auto it = tools_.find("plan.render");
      if (it != tools_.end()) {
        interfaces::ToolContext ctx{policy_};
        auto tr = it->second->invoke("plan_render", "{}", ctx);
        if (tr.ok && !tr.content.empty()) {
          console_.print_line(tr.content);
        } else {
          console_.print_line("(no plan)");
        }
      } else {
        console_.print_line("(plan tool not available)");
      }
      continue;
    }

    if (line->empty()) continue;
    handle_user_input(*line);
  }
}

void Agent::handle_user_input(const std::string& input) {
  // Merge plan into the existing base system prompt to avoid multiple system messages.
  if (auto it = tools_.find("plan.render"); it != tools_.end()) {
    interfaces::ToolContext ctx{policy_};
    auto tr = it->second->invoke("plan_render", "{}", ctx);
    if (tr.ok && !tr.content.empty()) {
      if (auto* sys = conv_.first_system_message(); sys) {
        sys->content = "You are a CLI coding assistant. Prefer concise answers.\n\n";
        sys->content += "Current plan:\n";
        sys->content += tr.content;
      }
    }
  }

  Message user;
  user.role = Role::kUser;
  user.content = input;
  conv_.add(user);

  storage_.append_log_line(std::string("user: ") + input);

  // First LLM call.
  auto resp = llm_.complete(conv_.messages(), llm_options_);

  // Tool loop.
  // Iterate until the assistant stops requesting tool calls, or we hit a safety cap.
  for (int iter = 0; iter < 8; ++iter) {
    if (resp.assistant_message.tool_calls.empty()) {
      conv_.add(resp.assistant_message);
      break;
    }

    // Add the assistant message that requested tools (must include tool_calls) before tool results.
    conv_.add(resp.assistant_message);

    interfaces::ToolContext ctx{policy_};

    for (const auto& tc : resp.assistant_message.tool_calls) {
      auto it = tools_.find(tc.name);
      if (it == tools_.end()) {
        ToolResult tr;
        tr.tool_call_id = tc.id;
        tr.ok = false;
        tr.content = "Tool not found: " + tc.name;
        console_.print_line(format_tool_log_line(tc, tr));

        Message tool_msg;
        tool_msg.role = Role::kTool;
        tool_msg.tool_result = tr;
        tool_msg.content = tr.content;
        conv_.add(std::move(tool_msg));
        continue;
      }

      auto tr = it->second->invoke(tc.id, tc.arguments_json, ctx);
      console_.print_line(format_tool_log_line(tc, tr));

      Message tool_msg;
      tool_msg.role = Role::kTool;
      tool_msg.tool_result = tr;
      tool_msg.content = tr.content;
      conv_.add(std::move(tool_msg));
    }

    resp = llm_.complete(conv_.messages(), llm_options_);
  }

  console_.print_line(resp.assistant_message.content);
  storage_.append_log_line(std::string("assistant: ") + resp.assistant_message.content);
}

} // namespace cpp_agent::core
