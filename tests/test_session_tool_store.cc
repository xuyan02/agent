#include "agent/session.h"

#include "tool/tool.h"

#include "dust/memory/ref_ptr.h"

#include <cassert>

namespace {

class DummyTool final : public agent::Tool {
 public:
  DummyTool() = default;

  const agent::ToolSpec* GetSpec() const override { return nullptr; }

  dust::FuturePtr<nlohmann::json> Invoke(dust::RefPtr<agent::AgentContext>,
                                        const std::string&,
                                        const nlohmann::json&) override {
    return nullptr;
  }
};

}  // namespace

int main() {
  agent::Session::Builder b;
  b.AddTool(dust::MakeRefPtr<DummyTool>());
  dust::RefPtr<agent::Session> s = std::move(b).Build();

  assert(s);
  assert(s->tools().size() == 1);
  assert(s->tools()[0]);

  return 0;
}
