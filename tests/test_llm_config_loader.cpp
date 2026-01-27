#include "infra/llm/llm_config_loader.h"
#include "infra/llm/llm_provider_factory.h"

#include <cassert>
#include <filesystem>
#include <fstream>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace {

struct FakeRequest final : public agent::LlmRequest {
  FakeRequest(std::vector<std::string>* events, std::string system_prompt, std::string user_prompt)
      : events_(events), system_prompt_(std::move(system_prompt)), user_prompt_(std::move(user_prompt)) {
    events_->push_back("connect");
    events_->push_back("send_system:" + system_prompt_);
    events_->push_back("send_user:" + user_prompt_);
  }

  ~FakeRequest() override {
    events_->push_back("disconnect");
  }

  std::vector<std::string>* events_;
  std::string system_prompt_;
  std::string user_prompt_;
};

class FakeProvider final : public agent::LlmProvider {
public:
  FakeProvider(std::string name, std::string model, std::vector<std::string>* events)
      : name_(std::move(name)), model_(std::move(model)), events_(events) {}

  std::string Name() const override { return name_; }

  bool SupportsModel(const std::string& model_name) const override { return model_name == model_; }

  std::unique_ptr<agent::LlmRequest> Create(
      std::string model_name,
      std::string system_prompt,
      std::string user_prompt,
      agent::LlmRequest::OnToken /*on_token*/,
      agent::LlmRequest::OnDone /*on_done*/) override {
    events_->push_back("provider_create:" + name_ + ":" + model_name);
    return std::make_unique<FakeRequest>(events_, std::move(system_prompt), std::move(user_prompt));
  }

private:
  std::string name_;
  std::string model_;
  std::vector<std::string>* events_;
};

class FakeProviderFactory final : public agent::LlmProviderFactory {
public:
  explicit FakeProviderFactory(std::vector<std::string>* events) : events_(events) {}

  std::string Name() const override { return "fake"; }

  std::unique_ptr<agent::LlmProvider> CreateFromConfig(
      std::string provider_name, std::vector<std::string> models, std::string /*params_json*/) const override {
    std::string joined;
    for (size_t i = 0; i < models.size(); i++) {
      if (i) joined += ",";
      joined += models[i];
    }
    events_->push_back("factory_create:" + provider_name + ":" + joined);

    const std::string model = models.empty() ? "" : models[0];
    return std::make_unique<FakeProvider>(std::move(provider_name), model, events_);
  }

private:
  std::vector<std::string>* events_;
};

std::filesystem::path write_temp(const std::string& content) {
  auto p = std::filesystem::temp_directory_path() / "cpp-agent-llm-providers.json";
  std::ofstream f(p);
  f << content;
  return p;
}

} // namespace

int main() {
  using agent::LlmContext;

  std::vector<std::string> events;

  LlmContext ctx;
  ctx.RegisterFactory(std::make_unique<FakeProviderFactory>(&events));

  const std::string json = R"JSON(
{
  "providers": [
    {"type": "fake", "name": "fake1", "models": ["model-x"], "params": {"k": "v"}}
  ]
}
)JSON";

  const auto path = write_temp(json);
  const bool ok = agent::RegisterProvidersFromConfig(ctx, path);
  assert(ok);

  {
    auto req = ctx.Create("model-x", "", "hello", {}, {});
    assert(req != nullptr);
  }

  const std::vector<std::string> expected = {
      "factory_create:fake1:model-x",
      "provider_create:fake1:model-x",
      "connect",
      "send_system:",
      "send_user:hello",
      "disconnect",
  };

  assert(events == expected);

  return 0;
}
