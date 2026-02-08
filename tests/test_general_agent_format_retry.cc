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
    assert(!steps_.empty() && "unexpected Create() call");
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

struct CapturingConsole final : public agent::IConsole {
  void PrintLine(const std::string& line) override {
    out += line;
    out += "\n";
  }
  void Print(const std::string& s) override { out += s; }
  void SetOnLine(dust::Function<void(std::string)> /*on_line*/) override {}

  std::string out;
};

static int count_system_format_errors(const std::vector<agent::LlmMessage>& msgs) {
  int n = 0;
  for (const auto& m : msgs) {
    if (m.role == agent::LlmRole::kSystem && m.content.find("FORMAT ERROR") != std::string::npos) n++;
  }
  return n;
}

} // namespace

int main() {
  // A) Missing @to: header => retry once, then emit on the second reply.
  {
    std::vector<std::vector<agent::LlmMessage>> seen;
    std::vector<ScriptedProvider::Step> steps;
    steps.push_back({.reply = "hello\n"});
    steps.push_back({.reply = "@master: ok\n"});

    CapturingConsole console;

    auto llm = std::make_unique<agent::LlmContext>();
    llm->Register(std::make_unique<ScriptedProvider>(&seen, steps));

    agent::Runtime rt(console, std::move(llm), std::filesystem::current_path());
    auto team = std::make_unique<agent::Team>(rt, "leader");
    agent::GeneralAgent a(*team, "gen", "m");

    a.Input({.from = "master", .to = "agent", .content = "first"});

    assert(seen.size() >= 2);
    assert(count_system_format_errors(seen[1]) == 1);
    assert(console.out.find("ok") != std::string::npos);
  }

  // B) Mixed [pause] + other output => retry.
  {
    std::vector<std::vector<agent::LlmMessage>> seen;
    std::vector<ScriptedProvider::Step> steps;
    steps.push_back({.reply = "[pause]\n@master: no\n"});
    steps.push_back({.reply = "[pause]\n"});

    CapturingConsole console;

    auto llm = std::make_unique<agent::LlmContext>();
    llm->Register(std::make_unique<ScriptedProvider>(&seen, steps));

    agent::Runtime rt(console, std::move(llm), std::filesystem::current_path());
    auto team = std::make_unique<agent::Team>(rt, "leader");
    agent::GeneralAgent a(*team, "gen", "m");

    a.Input({.from = "master", .to = "agent", .content = "first"});

    assert(seen.size() >= 2);
    assert(count_system_format_errors(seen[1]) == 1);
  }

  return 0;
}
