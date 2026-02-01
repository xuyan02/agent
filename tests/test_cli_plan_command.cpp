#include "runtime/runtime.h"

#include "llm/llm_context.h"
#include "interfaces/iconsole.h"
#include "runtime/team.h"

#include <cassert>
#include <string>

namespace agent {
namespace {

class CaptureConsole final : public agent::IConsole {
public:
  void PrintLine(const std::string& s) override { out += s + "\n"; }
  void Print(const std::string& s) override { out += s; }
  void SetOnLine(dust::Function<void(std::string)>) override {}

  std::string out;
};

} // namespace
} // namespace agent

int main() {
  agent::CaptureConsole console;

  // Runtime must have a team; /plan should not crash and should print something (even if empty).
  // We keep this as a smoke test for the /plan routing.
  auto llm = std::make_unique<agent::LlmContext>();
  agent::Runtime runtime(console, std::move(llm), std::filesystem::current_path());

  auto team = std::make_unique<agent::Team>(runtime, "leader");
  team->Add(std::make_unique<agent::GeneralAgent>(*team, "leader", "dummy-model"));
  runtime.SetTeam(std::move(team));

  runtime.OnCliLine("/plan");

  // Smoke: command routed and produced some output (may be empty if no tasks).
  assert(!console.out.empty());

  console.out.clear();
  runtime.OnCliLine("/prompt");
  // System prompt must include at least the plan header.
  assert(console.out.find("## Plan") != std::string::npos);

  return 0;
}
