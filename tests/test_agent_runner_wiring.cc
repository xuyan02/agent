#include "agent/agent_runner.h"

#include "agent/agent_context.h"
#include "agent/in_memory_history.h"
#include "console/console.h"

#include "dust/memory/ref_ptr.h"

#include <cassert>
#include <string>
#include <utility>

namespace {

class FakeConsole final : public agent::Console {
 public:
  void PrintLine(const std::string&) override {}
  void Print(const std::string&) override {}

  void SetOnLine(dust::Function<void(std::string)> on_line) override {
    on_line_ = std::move(on_line);
  }

  bool has_on_line() const { return !!on_line_; }

  void EmitLine(std::string line) {
    if (on_line_)
      on_line_(std::move(line));
  }

 private:
  dust::Function<void(std::string)> on_line_;
};

class InspectAgent final : public agent::Agent {
 public:
  dust::FuturePtr<dust::Result<void, std::string>> Run(
      dust::RefPtr<agent::AgentContext> context) override {
    assert(context);
    assert(context->session());
    assert(context->history());
    assert(dynamic_cast<agent::InMemoryHistory*>(context->history().Get()) != nullptr);
    assert(context->system_segments().empty());
    assert(context->tools().empty());

    ran_ = true;

    class DoneFuture final : public dust::Future<dust::Result<void, std::string>> {
     public:
      dust::Poll<dust::Result<void, std::string>> PollOnce(dust::PollContext&) override {
        if (done_)
          return dust::Poll<dust::Result<void, std::string>>::Ready(
              dust::Result<void, std::string>::Err("polled twice"));
        done_ = true;
        return dust::Poll<dust::Result<void, std::string>>::Ready(
            dust::Result<void, std::string>::Ok());
      }

     private:
      bool done_ = false;
    };

    return dust::MakeRefPtr<DoneFuture>();
  }

  bool ran() const { return ran_; }

 private:
  bool ran_ = false;
};

}  // namespace

int main() {
  auto agent = std::make_unique<InspectAgent>();
  InspectAgent* raw = agent.get();

  agent::AgentRunner runner(std::move(agent));
  dust::RefPtr<agent::Session> session = std::move(agent::Session::Builder()).Build();

  auto console = std::make_unique<FakeConsole>();
  FakeConsole* raw_console = console.get();

  runner.Run(session, std::move(console));

  // Runner should bind the console callback; emitting a line should drive agent_->Run(ctx).
  raw_console->EmitLine("hello");

  // AgentRunner spawns work only if MessageLoop::Current() is non-null.
  // This wiring test runs without a loop, so we only verify the callback is wired.
  assert(raw_console->has_on_line());

  return 0;
}
