#include "infra/llm/openai_provider.h"

#include "core/tool_protocol.h"
#include "interfaces/illm_client.h"

#include <utility>

namespace cpp_agent::infra::llm {
namespace {

bool contains_model(const std::vector<std::string>& models, const std::string& model_name) {
  for (const auto& m : models) {
    if (m == model_name) return true;
  }
  return false;
}

} // namespace

OpenAIRequest::OpenAIRequest(std::shared_ptr<cpp_agent::infra::http::OpenAIClient> client,
                             std::string model_name,
                             std::string prompt)
    : client_(std::move(client)) {
  // "Connect + send" happens here.
  cpp_agent::interfaces::LlmOptions opt;
  opt.model = std::move(model_name);

  cpp_agent::core::Message msg;
  msg.role = cpp_agent::core::Role::kUser;
  msg.content = std::move(prompt);

  std::vector<cpp_agent::core::Message> messages;
  messages.push_back(std::move(msg));

  (void)client_->complete(messages, opt);
}

OpenAIRequest::~OpenAIRequest() = default;

OpenAIProvider::OpenAIProvider(std::string name,
                               std::vector<std::string> models,
                               std::string base_url,
                               std::string api_key)
    : name_(std::move(name)),
      models_(std::move(models)),
      base_url_(std::move(base_url)),
      api_key_(std::move(api_key)) {}

bool OpenAIProvider::SupportsModel(const std::string& model_name) const {
  return contains_model(models_, model_name);
}

std::unique_ptr<LlmRequest> OpenAIProvider::Create(std::string model_name, std::string prompt) {
  auto client = std::make_shared<cpp_agent::infra::http::OpenAIClient>(base_url_, api_key_);
  return std::make_unique<OpenAIRequest>(std::move(client), std::move(model_name), std::move(prompt));
}

} // namespace cpp_agent::infra::llm
