#include "runtime/professional_agent.h"

#include "runtime/runtime.h"
#include "runtime/team.h"

#include "infra/console/cli_console.h"

#include <cassert>
#include <memory>
#include <string>
#include <utility>

namespace {

class FakeConsole final : public agent::IConsole {
public:
  void PrintLine(const std::string& /*line*/) override {}
  void Print(const std::string& /*s*/) override {}
  void SetOnLine(dust::Function<void(std::string)> /*on_line*/) override {}
};

struct ImmediateRequest final : public agent::LlmRequest {
  ImmediateRequest(std::string payload,
                   agent::LlmRequest::OnToken on_token,
                   agent::LlmRequest::OnDone on_done)
      : payload_(std::move(payload)), on_token_(std::move(on_token)), on_done_(std::move(on_done)) {
    if (on_token_) on_token_(payload_);
    if (on_done_) on_done_();
  }

  std::string payload_;
  agent::LlmRequest::OnToken on_token_;
  agent::LlmRequest::OnDone on_done_;
};

class FakeProvider final : public agent::LlmProvider {
public:
  std::string Name() const override { return "fake"; }
  bool SupportsModel(const std::string& model) const override { return model == "m"; }

  std::unique_ptr<agent::LlmRequest> Create(std::string /*model_name*/,
                                           std::string /*system_prompt*/,
                                           std::string user_prompt,
                                           agent::LlmRequest::OnToken on_token,
                                           agent::LlmRequest::OnDone on_done) override {
    return std::make_unique<ImmediateRequest>("echo:" + user_prompt, std::move(on_token), std::move(on_done));
  }
};

} // namespace

int main() {
  FakeConsole console;

  auto llm = std::make_unique<agent::LlmContext>();
  llm->Register(std::make_unique<FakeProvider>());

  agent::Runtime runtime(console, std::move(llm), std::filesystem::current_path());
  auto team = std::make_unique<agent::Team>(runtime, "leader");

  agent::ProfessionalAgent a(*team, "pro", "m");

  std::string got;
  a.Input("hi", [&](const std::string& s) { got = s; });
  assert(got == "echo:hi");

  return 0;
}
