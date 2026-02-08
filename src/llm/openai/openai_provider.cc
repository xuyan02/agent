#include "llm/openai/openai_provider.h"

#include "llm/openai/openai_sender.h"

#include <utility>

namespace agent {

OpenAiProvider::OpenAiProvider(std::string base_url, std::string api_key)
    : base_url_(std::move(base_url)), api_key_(std::move(api_key)) {}

OpenAiProvider::~OpenAiProvider() = default;

std::unique_ptr<LlmSender> OpenAiProvider::CreateSender(std::string model_name) {
  return std::make_unique<OpenAiSender>(base_url_, api_key_, std::move(model_name));
}

}  // namespace agent
