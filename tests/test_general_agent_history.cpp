#include "runtime/general_agent.h"

#include "infra/llm/llm_context.h"
#include "runtime/runtime.h"
#include "runtime/team.h"
#include "interfaces/iconsole.h"

#include <cassert>
#include <memory>
#include <string>
#include <vector>

#include <filesystem>

namespace {

struct FakeRequest final : public agent::LlmRequest {};

class CaptureProvider final : public agent::LlmProvider {
public:
  explicit CaptureProvider(std::vector<std::vector<agent::LlmMessage>>* seen)
      : seen_(seen) {}

  std::string Name() const override { return "capture"; }

  bool SupportsModel(const std::string& /*model*/) const override { return true; }

  std::unique_ptr<agent::LlmRequest> Create(std::string /*model_name*/,
                                           std::vector<agent::LlmMessage> messages,
                                           std::vector<agent::Tool> /*tools*/,
                                           agent::LlmRequest::OnToken on_token,
                                           agent::LlmRequest::OnToolCalls /*on_tool_calls*/,
                                           agent::LlmRequest::OnDone on_done) override {
    seen_->push_back(messages);
    if (on_token) on_token("@master:\nhello\n");
    if (on_done) std::move(on_done)();
    return std::make_unique<FakeRequest>();
  }

private:
  std::vector<std::vector<agent::LlmMessage>>* seen_;
};

} // namespace

int main() {
  std::vector<std::vector<agent::LlmMessage>> seen;

  struct FakeConsole final : public agent::IConsole {
    void PrintLine(const std::string& /*line*/) override {}
    void Print(const std::string& /*s*/) override {}
    void SetOnLine(dust::Function<void(std::string)> /*on_line*/) override {}
  };

  FakeConsole console;

  auto llm = std::make_unique<agent::LlmContext>();
  llm->Register(std::make_unique<CaptureProvider>(&seen));

  agent::Runtime rt(console, std::move(llm), std::filesystem::current_path());
  auto team = std::make_unique<agent::Team>(rt, "leader");

  agent::GeneralAgent a(*team, "gen", "m");

  a.Input({.from = "master", .to = "agent", .content = "first"});
  a.Input({.from = "master", .to = "agent", .content = "second"});

  // Two calls.
  assert(seen.size() == 2);

  // Second call should include assistant message from first call.
  bool has_prev_assistant = false;
  for (const auto& m : seen[1]) {
    if (m.role == agent::LlmRole::kAssistant && m.content.find("@master") != std::string::npos) {
      has_prev_assistant = true;
      break;
    }
  }
  assert(has_prev_assistant);

  return 0;
}
