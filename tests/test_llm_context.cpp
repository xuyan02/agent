#include "infra/llm/llm_context.h"

#include <cassert>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace {

struct FakeRequest final : public agent::LlmRequest {
  FakeRequest(std::vector<std::string>* events, std::string prompt)
      : events_(events), prompt_(std::move(prompt)) {
    events_->push_back("connect");
    events_->push_back("send:" + prompt_);
  }

  ~FakeRequest() override {
    events_->push_back("disconnect");
  }

  std::vector<std::string>* events_;
  std::string prompt_;
};

class FakeProvider final : public agent::LlmProvider {
public:
  FakeProvider(std::string name, std::vector<std::string> models,
               std::vector<std::string>* events)
      : name_(std::move(name)), models_(std::move(models)), events_(events) {}

  std::string Name() const override { return name_; }

  bool SupportsModel(const std::string& model_name) const override {
    for (const auto& m : models_) {
      if (m == model_name) return true;
    }
    return false;
  }

  std::unique_ptr<agent::LlmRequest> Create(
      std::string model_name,
      std::string prompt,
      agent::LlmRequest::OnToken /*on_token*/,
      agent::LlmRequest::OnDone /*on_done*/) override {
    events_->push_back("provider_create:" + name_ + ":" + model_name);
    return std::make_unique<FakeRequest>(events_, std::move(prompt));
  }

private:
  std::string name_;
  std::vector<std::string> models_;
  std::vector<std::string>* events_;
};

} // namespace

int main() {
  using agent::LlmContext;

  LlmContext reg;
  reg.Clear();

  std::vector<std::string> events;

  reg.Register(std::make_unique<FakeProvider>("openai", std::vector<std::string>{"model-a"}, &events));
  reg.Register(std::make_unique<FakeProvider>("openai", std::vector<std::string>{"model-b"}, &events));
  reg.Register(std::make_unique<FakeProvider>("other", std::vector<std::string>{"model-a"}, &events));

  // Unknown model returns nullptr.
  {
    auto req = reg.Create("missing", "hi", {}, {});
    assert(req == nullptr);
  }

  // Model selection picks the FIRST provider that supports it.
  {
    auto req = reg.Create("model-a", "hello", {}, {});
    assert(req != nullptr);
  }

  // This should be served by the second provider.
  {
    auto req = reg.Create("model-b", "world", {}, {});
    assert(req != nullptr);
  }

  const std::vector<std::string> expected = {
      "provider_create:openai:model-a",
      "connect",
      "send:hello",
      "disconnect",
      "provider_create:openai:model-b",
      "connect",
      "send:world",
      "disconnect",
  };
  assert(events == expected);

  return 0;
}
