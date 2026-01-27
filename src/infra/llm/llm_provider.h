#pragma once

#include "infra/llm/llm_request.h"
#include "runtime/tool.h"

#include <memory>
#include <string>
#include <vector>

namespace agent {

class LlmProvider {
public:
  virtual ~LlmProvider() = default;

  LlmProvider(const LlmProvider&) = delete;
  LlmProvider& operator=(const LlmProvider&) = delete;

  // Provider name (e.g. "openai"). Not required to be unique.
  virtual std::string Name() const = 0;

  // Whether this provider supports the given model name.
  virtual bool SupportsModel(const std::string& model_name) const = 0;

  // Must create a request that connects + sends in its constructor.
  virtual std::unique_ptr<LlmRequest> Create(std::string model_name,
                                             std::string system_prompt,
                                             std::string user_prompt,
                                             std::vector<agent::Tool> tools,
                                             LlmRequest::OnToken on_token,
                                             LlmRequest::OnDone on_done) = 0;

protected:
  LlmProvider() = default;
};

} // namespace agent
