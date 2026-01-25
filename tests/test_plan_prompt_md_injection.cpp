#include "core/agent.h"
#include "core/policy.h"
#include "infra/plan/plan_store.h"
#include "interfaces/illm_client.h"

#include <cassert>
#include <filesystem>
#include <memory>
#include <unordered_map>

namespace {

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
};

class NullStorage final : public cpp_agent::interfaces::IStorage {
public:
  void append_log_line(const std::string& /*line*/) override {}
};

class CapturingLlm final : public cpp_agent::interfaces::ILlmClient {
public:
  cpp_agent::interfaces::LlmResponse complete(const std::vector<cpp_agent::core::Message>& messages,
                                             const cpp_agent::interfaces::LlmOptions& /*options*/) override {
    for (const auto& m : messages) {
      if (m.role == cpp_agent::core::Role::kSystem) {
        last_system = m.content;
      }
    }
    cpp_agent::interfaces::LlmResponse resp;
    resp.assistant_message.role = cpp_agent::core::Role::kAssistant;
    resp.assistant_message.content = "done";
    return resp;
  }

  std::string last_system;
};

} // namespace

int main() {
  CapturingLlm llm;
  OneShotConsole console;
  NullStorage storage;

  cpp_agent::core::Policy policy(std::filesystem::current_path());

  auto plan_path = std::filesystem::temp_directory_path() / "cpp-agent-test-plan-prompt-md.json";
  std::error_code ec;
  std::filesystem::remove(plan_path, ec);
  auto store = std::make_shared<cpp_agent::infra::plan::PlanStore>(plan_path);
  store->load();

  std::unordered_map<std::string, std::unique_ptr<cpp_agent::interfaces::ITool>> tools;

  cpp_agent::interfaces::LlmOptions opt;
  opt.model = "fake";

  std::string md = "# Plan rules\nAlways update plan.\n";
  cpp_agent::core::Agent agent(llm, console, storage, std::move(policy), std::move(tools), opt, *store, md);
  agent.repl();

  assert(llm.last_system.find("# Plan rules") != std::string::npos);
  assert(llm.last_system.find("Current plan:") != std::string::npos);
  return 0;
}
