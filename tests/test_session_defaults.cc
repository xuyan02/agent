#include "agent/session.h"

#include "agent/in_memory_history.h"
#include "agent/system_prompt_segment.h"

#include "dust/memory/ref_ptr.h"

#include <cassert>
#include <filesystem>
#include <string>

namespace {

class ConstSegment final : public agent::SystemPromptSegment {
 public:
  explicit ConstSegment(std::string s) : s_(std::move(s)) {}

  dust::FuturePtr<std::string> Build(dust::RefPtr<agent::AgentContext>) override {
    class F final : public dust::Future<std::string> {
     public:
      explicit F(std::string v) : v_(std::move(v)) {}

      dust::Poll<std::string> PollOnce(dust::PollContext&) override {
        if (done_)
          return dust::Poll<std::string>::Ready(std::string());
        done_ = true;
        return dust::Poll<std::string>::Ready(std::move(v_));
      }

     private:
      std::string v_;
      bool done_ = false;
    };

    return dust::MakeRefPtr<F>(s_);
  }

 private:
  std::string s_;
};

}  // namespace

int main() {
  // Default history + empty segments.
  dust::RefPtr<agent::Session> s = std::move(agent::Session::Builder()).Build();
  assert(s);
  assert(s->history());
  assert(dynamic_cast<agent::InMemoryHistory*>(s->history().Get()) != nullptr);
  assert(s->system_segments().empty());
  assert(s->tools().empty());
  assert(s->llm_providers().empty());
  assert(s->default_model().empty());

  assert(!s->workspace_path().empty());
  assert(s->workspace_path() == std::filesystem::current_path());
  assert(s->agent_path() == (s->workspace_path() / ".agent"));

  // Segments/tools are empty by default.

  // Can add segments; Session only stores them.
  agent::Session::Builder b;
  b.AddSystemSegment(dust::MakeRefPtr<ConstSegment>("a"));
  b.AddSystemSegment(dust::MakeRefPtr<ConstSegment>("b"));
  dust::RefPtr<agent::Session> s2 = std::move(b).Build();
  assert(s2);
  assert(s2->system_segments().size() == 2);

  return 0;
}
