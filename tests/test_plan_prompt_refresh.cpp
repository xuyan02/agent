#include "core/agent.h"
#include "core/policy.h"
#include "infra/plan/plan_store.h"
#include "infra/tools/plan_toolset.h"
#include "interfaces/illm_client.h"

#include <cassert>
#include <filesystem>
#include <memory>
#include <unordered_map>

namespace {

class FakeConsole final : public cpp_agent::interfaces::IConsole {
public:
  void print_line(const std::string& /*line*/) override {}
  std::optional<std::string> read_line(const std::string& /*prompt*/) override { return std::nullopt; }
};

class NullStorage final : public cpp_agent::interfaces::IStorage {
public:
  void append_log_line(const std::string& /*line*/) override {}
};

class FakeLlm final : public cpp_agent::interfaces::ILlmClient {
public:
  cpp_agent::interfaces::LlmResponse complete(const std::vector<cpp_agent::core::Message>& messages,
                                             const cpp_agent::interfaces::LlmOptions& /*options*/) override {
    calls++;

    // Ensure system prompt always includes latest plan and plan tool guidance just before each call.
    auto has = [&](const std::string& needle) {
      for (const auto& m : messages) {
        if (m.role == cpp_agent::core::Role::kSystem && m.content.find(needle) != std::string::npos) return true;
      }
      return false;
    };

    if (calls == 1) {
      // Before first call: plan should not include our soon-to-be-added title.
      assert(!has("Task A"));

      cpp_agent::interfaces::LlmResponse resp;
      resp.assistant_message.role = cpp_agent::core::Role::kAssistant;
      cpp_agent::core::ToolCall tc;
      tc.id = "call_add";
      tc.name = "plan_add";
      tc.arguments_json = "{\"goal\":\"g\",\"title\":\"Task A\"}";
      resp.assistant_message.tool_calls.push_back(std::move(tc));
      return resp;
    }

    if (calls == 2) {
      // After plan_add tool ran: next call must include updated plan.
      assert(has("Task A"));

      cpp_agent::interfaces::LlmResponse resp;
      resp.assistant_message.role = cpp_agent::core::Role::kAssistant;
      resp.assistant_message.content = "done";
      return resp;
    }

    assert(false && "unexpected extra llm call");
    return {};
  }

  int calls{0};
};

} // namespace

int main() {
  FakeLlm llm;
  FakeConsole console;
  NullStorage storage;

  cpp_agent::core::Policy policy(std::filesystem::current_path());

  auto plan_path = std::filesystem::temp_directory_path() / "cpp-agent-test-plan-refresh.json";
  std::error_code ec;
  std::filesystem::remove(plan_path, ec);
  auto store = std::make_shared<cpp_agent::infra::plan::PlanStore>(plan_path);
  store->load();

  std::unordered_map<std::string, std::unique_ptr<cpp_agent::interfaces::ITool>> tools;
  tools.emplace("plan_add", std::make_unique<cpp_agent::infra::tools::PlanAddTool>(store));

  cpp_agent::interfaces::LlmOptions opt;
  opt.model = "fake";

  // Drive via repl() since handle_user_input is private.
  class OneShotConsole final : public cpp_agent::interfaces::IConsole {
  public:
    void print_line(const std::string& /*line*/) override {}
    std::optional<std::string> read_line(const std::string& /*prompt*/) override {
      if (!sent_) {
        sent_ = true;
        return std::string("hi");
      }
      return std::string("/exit");
    }

  private:
    bool sent_{false};
  } repl_console;

  cpp_agent::core::Agent agent2(llm, repl_console, storage, cpp_agent::core::Policy(std::filesystem::current_path()),
                               std::move(tools), opt, *store, "");
  agent2.repl();

  assert(llm.calls == 2);
  return 0;
}
