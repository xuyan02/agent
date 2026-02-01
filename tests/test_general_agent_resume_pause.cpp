#include "llm/llm_context.h"
#include "runtime/runtime.h"
#include "runtime/team.h"
#include "interfaces/iconsole.h"

#include <cassert>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

namespace {

struct FakeRequest final : public agent::LlmRequest {};

class ScriptedProvider final : public agent::LlmProvider {
public:
  struct Step {
    std::string reply;
  };

  explicit ScriptedProvider(std::vector<std::vector<agent::LlmMessage>>* seen, std::vector<Step> steps)
      : seen_(seen), steps_(std::move(steps)) {}

  std::string Name() const override { return "scripted"; }

  bool SupportsModel(const std::string& /*model*/) const override { return true; }

  std::unique_ptr<agent::LlmRequest> Create(std::string /*model_name*/,
                                           std::vector<agent::LlmMessage> messages,
                                           std::vector<agent::Tool> /*tools*/,
                                           agent::LlmRequest::OnToken on_token,
                                           agent::LlmRequest::OnToolCalls /*on_tool_calls*/,
                                           agent::LlmRequest::OnDone on_done) override {
    seen_->push_back(messages);

    if (steps_.empty()) {
      assert(false && "unexpected Create() call");
    }

    auto step = steps_.front();
    steps_.erase(steps_.begin());

    if (on_token) on_token(step.reply);
    if (on_done) std::move(on_done)();
    return std::make_unique<FakeRequest>();
  }

private:
  std::vector<std::vector<agent::LlmMessage>>* seen_;
  std::vector<Step> steps_;
};

struct FakeConsole final : public agent::IConsole {
  void PrintLine(const std::string& /*line*/) override {}
  void Print(const std::string& /*s*/) override {}
  void SetOnLine(dust::Function<void(std::string)> /*on_line*/) override {}
};

static bool has_last_user_content(const std::vector<agent::LlmMessage>& msgs, const std::string& want) {
  for (auto it = msgs.rbegin(); it != msgs.rend(); ++it) {
    if (it->role == agent::LlmRole::kUser) {
      return it->content == want;
    }
  }
  return false;
}

} // namespace

int main() {
  std::vector<std::vector<agent::LlmMessage>> seen;

  // Step1: normal reply => should auto-trigger a resume tick.
  // Step2: pause => should stop auto-triggering.
  std::vector<ScriptedProvider::Step> steps;
  steps.push_back({.reply = "@master: ok\n"});
  steps.push_back({.reply = "[pause]\n"});

  FakeConsole console;

  auto llm = std::make_unique<agent::LlmContext>();
  llm->Register(std::make_unique<ScriptedProvider>(&seen, steps));

  agent::Runtime rt(console, std::move(llm), std::filesystem::current_path());
  auto team = std::make_unique<agent::Team>(rt, "leader");

  agent::GeneralAgent a(*team, "gen", "m");

  a.Input({.from = "master", .to = "agent", .content = "first"});

  // We expect two LLM calls:
  // 1) for the external input
  // 2) resume tick because queue becomes empty after the first batch
  assert(seen.size() == 2);
  assert(has_last_user_content(seen[1], "[resume]\n"));

  // Now feed another input; agent should accept new work and send another request.
  a.Input({.from = "master", .to = "agent", .content = "second"});
  assert(seen.size() == 3);

  return 0;
}
