#include "core/agent.h"

#include "core/policy.h"
#include "infra/console/cli_console.h"
#include "infra/storage/json_file_storage.h"
#include "infra/tools/plan_toolset.h"
#include "interfaces/illm_client.h"

#include <cassert>
#include <filesystem>
#include <memory>
#include <unordered_map>

namespace {

class FakeConsole final : public cpp_agent::interfaces::IConsole {
public:
  void print_line(const std::string& line) override { last_line = line; }

  std::optional<std::string> read_line(const std::string& /*prompt*/) override {
    if (read_line_result) {
      auto out = read_line_result;
      read_line_result.reset();
      return out;
    }
    return std::string("/exit");
  }

  std::optional<std::string> read_line_result;
  std::string last_line;
};

class FakeLlm final : public cpp_agent::interfaces::ILlmClient {
public:
  cpp_agent::interfaces::LlmResponse complete(const std::vector<cpp_agent::core::Message>& messages,
                                             const cpp_agent::interfaces::LlmOptions& /*options*/) override {
    calls++;
    last_messages = messages;

    cpp_agent::interfaces::LlmResponse resp;
    resp.assistant_message.role = cpp_agent::core::Role::kAssistant;

    if (calls == 1) {
      cpp_agent::core::ToolCall tc;
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
      if (m.role == cpp_agent::core::Role::kAssistant && !m.tool_calls.empty()) {
        saw_assistant_with_tool_calls = true;
      }
      if (m.role == cpp_agent::core::Role::kTool && m.tool_result && m.tool_result->tool_call_id == "call_test_1") {
        saw_tool = true;
      }
    }

    assert(saw_assistant_with_tool_calls);
    assert(saw_tool);

    resp.assistant_message.content = "done";
    return resp;
  }

  int calls{0};
  std::vector<cpp_agent::core::Message> last_messages;
};

class NullStorage final : public cpp_agent::interfaces::IStorage {
public:
  void append_log_line(const std::string& /*line*/) override {}
};

} // namespace

int main() {
  FakeLlm llm;
  FakeConsole console;
  NullStorage storage;

  cpp_agent::core::Policy policy(std::filesystem::current_path());

  // Feed a repl command then one line of user input.
  console.read_line_result = std::string("/plan");

  auto plan_path = std::filesystem::temp_directory_path() / "cpp-agent-test-plan.json";
  std::error_code ec;
  std::filesystem::remove(plan_path, ec);
  auto store = std::make_shared<cpp_agent::infra::plan::PlanStore>(plan_path);
  store->load();

  std::unordered_map<std::string, std::unique_ptr<cpp_agent::interfaces::ITool>> tools;

  cpp_agent::interfaces::LlmOptions opt;
  opt.model = "fake";

  cpp_agent::core::Agent agent(llm, console, storage, std::move(policy), std::move(tools), opt, *store, "");

  // Drive via repl() since handle_user_input is private.
  agent.repl();

  // /plan prints the rendered markdown from plan.render tool.
  assert(console.last_line.find("# Tasks") != std::string::npos);

  // /plan should not contact the LLM.
  assert(llm.calls == 0);
  return 0;
}
