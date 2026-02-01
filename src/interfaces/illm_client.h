#pragma once

#include "infra/llm/llm_message.h"

#include <string>
#include <vector>

namespace agent {

struct LlmOptions {
  std::string model;
  double temperature{0.0};
};

struct LlmResponse {
  agent::LlmMessage assistant_message;
};

class ILlmClient {
public:
  virtual ~ILlmClient() = default;
  virtual LlmResponse Complete(const std::vector<agent::LlmMessage>& messages,
                               const LlmOptions& options) = 0;
};

} // namespace agent
