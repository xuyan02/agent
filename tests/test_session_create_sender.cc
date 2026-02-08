#include "agent/session.h"

#include "dust/memory/ref_ptr.h"
#include "llm/llm_sender.h"

#include <cassert>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace {

class DummySender final : public agent::LlmSender {
 public:
  explicit DummySender(std::string model) : agent::LlmSender(std::move(model)) {}

  dust::FuturePtr<dust::Result<dust::RefPtr<agent::ChatMessage>, std::string>> Send(
      std::vector<dust::RefPtr<agent::ChatMessage>>, std::vector<agent::Tool*>) override {
    return nullptr;
  }
};

class ProviderNever final : public agent::LlmProvider {
 public:
  std::unique_ptr<agent::LlmSender> CreateSender(std::string) override { return nullptr; }
};

class ProviderMatch final : public agent::LlmProvider {
 public:
  explicit ProviderMatch(std::string wanted) : wanted_(std::move(wanted)) {}

  std::unique_ptr<agent::LlmSender> CreateSender(std::string model_name) override {
    if (model_name != wanted_)
      return nullptr;
    return std::make_unique<DummySender>(std::move(model_name));
  }

 private:
  std::string wanted_;
};

}  // namespace

int main() {
  agent::Session::Builder b;
  b.SetDefaultModel("m-default");
  b.AddLlmProvider(std::make_unique<ProviderNever>());
  b.AddLlmProvider(std::make_unique<ProviderMatch>("m1"));

  dust::RefPtr<agent::Session> s = std::move(b).Build();
  assert(s);
  assert(s->default_model() == "m-default");

  // First matching provider wins.
  std::unique_ptr<agent::LlmSender> sender = s->CreateSender("m1");
  assert(sender);

  // No match.
  sender = s->CreateSender("m2");
  assert(!sender);

  return 0;
}
