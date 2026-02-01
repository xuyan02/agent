#include "infra/llm/llm_context.h"

#include <cstdlib>
#include <iostream>
#include <string.h>

namespace agent {
namespace {

bool DebugLlm() {
  const char* v = std::getenv("CPP_AGENT_DEBUG_LLM_VERBOSE");
  return v && *v && strcmp(v, "0") != 0;
}

} // namespace

void LlmContext::Register(std::unique_ptr<LlmProvider> provider) {
  if (!provider) return;
  providers_.push_back(std::move(provider));
}

void LlmContext::RegisterFactory(std::unique_ptr<LlmProviderFactory> factory) {
  if (!factory) return;
  factories_.push_back(std::move(factory));
}

const LlmProviderFactory* LlmContext::FindProviderFactory(const std::string& provider_name) const {
  for (const auto& f : factories_) {
    if (!f) continue;
    if (f->Name() == provider_name) return f.get();
  }
  return nullptr;
}

std::unique_ptr<LlmRequest> LlmContext::Create(std::string model_name,
                                              std::vector<LlmMessage> messages,
                                              std::vector<agent::Tool*> tools,
                                              LlmRequest::OnToken on_token,
                                              LlmRequest::OnToolCalls on_tool_calls,
                                              LlmRequest::OnDone on_done) {
  if (DebugLlm()) {
    std::cerr << "[cpp-agent.llm] Create model=" << model_name << " providers=" << providers_.size()
              << std::endl;
  }

  for (auto& provider : providers_) {
    if (!provider) continue;
    if (!provider->SupportsModel(model_name)) continue;
    if (DebugLlm()) {
      std::cerr << "[cpp-agent.llm] selected provider name=" << provider->Name() << std::endl;
    }
    return provider->Create(std::move(model_name),
                            std::move(messages),
                            std::move(tools),
                            std::move(on_token),
                            std::move(on_tool_calls),
                            std::move(on_done));
  }

  if (DebugLlm()) {
    std::cerr << "[cpp-agent.llm] no provider supports model=" << model_name << std::endl;
  }
  return nullptr;
}

void LlmContext::Clear() {
  providers_.clear();
  factories_.clear();
}

} // namespace agent
