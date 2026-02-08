#include "runtime/runtime.h"

#include "interfaces/iconsole.h"

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

  agent::Runtime runtime(console, "");
  assert(runtime.Init());

  // smoke: prompt load path exists and is non-empty (repo provides prompts/)
  const std::string p = runtime.GetPrompt("intuitive");
  assert(!p.empty());

  return 0;
}
