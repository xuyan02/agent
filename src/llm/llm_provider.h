#pragma once

#include "llm/llm_sender.h"

#include <memory>
#include <string>

namespace agent {

class LlmProvider {
public:
  virtual ~LlmProvider() = default;

  LlmProvider(const LlmProvider&) = delete;
  LlmProvider& operator=(const LlmProvider&) = delete;

  virtual std::unique_ptr<LlmSender> CreateSender(std::string model_name) = 0;

protected:
  LlmProvider() = default;
};

} // namespace agent
