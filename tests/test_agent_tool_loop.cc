#include "dust/message_loop/linux_message_pump_epoll.h"
#include "dust/message_loop/message_loop.h"

#include "core/agent.h"

#include "core/policy.h"
#include "console/cli_console.h"
#include "storage/json_file_storage.h"
#include "infra/tools/plan_toolset.h"
#include "interfaces/illm_client.h"

#include <cassert>
#include <filesystem>
#include <memory>
#include <unordered_map>

namespace {

class FakeConsole final : public agent::IConsole {
public:
  void PrintLine(const std::string& line) override { last_line = line; }
  void Print(const std::string& s) override { stream += s; }

  void SetOnLine(dust::Function<void(std::string)> on_line) override { on_line_ = std::move(on_line); }

  void EmitLine(std::string line) {
    if (on_line_) on_line_(std::move(line));
  }

  dust::Function<void(std::string)> on_line_;
  std::string last_line;
  std::string stream;
};

class FakeLlm final : public agent::ILlmClient {
public:
  agent::LlmResponse Complete(const std::vector<agent::Message>& messages,
                                             const agent::LlmOptions& /*options*/) override {
    calls++;
    last_messages = messages;

    agent::LlmResponse resp;
    resp.assistant_message.role = agent::Role::kAssistant;

    if (calls == 1) {
      agent::ToolCall tc;
      tc.id = "call_test_1";
      tc.name = "plan.render";
      tc.arguments_json = "{}";
      resp.assistant_message.content = "";
      resp.assistant_message.tool_calls.push_back(std::move(tc));
      return resp;
    }

    // Second call should include prior assistant tool_calls and the tool result.
    bool saw_assistant_with_tool_calls = false;
    bool saw_tool = false;
    for (const auto& m : messages) {
      if (m.role == agent::Role::kAssistant && !m.tool_calls.empty()) {
        saw_assistant_with_tool_calls = true;
      }
      if (m.role == agent::Role::kTool && m.tool_result && m.tool_result->tool_call_id == "call_test_1") {
        saw_tool = true;
      }
    }

    assert(saw_assistant_with_tool_calls);
    assert(saw_tool);

    resp.assistant_message.content = "done";
    return resp;
  }

  int calls{0};
  std::vector<agent::Message> last_messages;
};

class NullStorage final : public agent::IStorage {
public:
  void AppendLogLine(const std::string& /*line*/) override {}
};

} // namespace

int main() {
  FakeLlm llm;
  FakeConsole console;
  NullStorage storage;

  agent::Policy policy(std::filesystem::current_path());

  // Feed a repl command then one line of user input.
  // Provide an initial line when the agent sets the callback.

  auto plan_path = std::filesystem::temp_directory_path() / "cpp-agent-test-plan.json";
  std::error_code ec;
  std::filesystem::remove(plan_path, ec);
  auto store = std::make_shared<agent::PlanStore>(plan_path);
  store->load();

  std::unordered_map<std::string, std::unique_ptr<agent::ITool>> tools;

  agent::LlmOptions opt;
  opt.model = "fake";

  agent::Agent agent(llm, console, storage, std::move(policy), std::move(tools), opt, *store, "");

  // Drive via Repl() since handle_user_input is private.
  dust::MessageLoop loop(std::make_unique<dust::LinuxMessagePumpEpoll>());
  agent.Repl(loop);

  console.EmitLine("/plan");

  // /plan prints the rendered markdown from plan.render tool.
  assert(console.last_line.find("# Tasks") != std::string::npos);

  // /plan should not contact the LLM.
  assert(llm.calls == 0);
  return 0;
}
