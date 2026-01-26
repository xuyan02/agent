#pragma once

#include "core/tool_protocol.h"

#include <string>
#include <vector>

namespace agent {

struct LlmOptions {
  std::string model;
  double temperature{0.2};
};

struct LlmResponse {
  agent::Message assistant_message;
};

class ILlmClient {
public:
  virtual ~ILlmClient() = default;
  virtual LlmResponse Complete(const std::vector<agent::Message>& messages,
                               const LlmOptions& options) = 0;
};

} // namespace agent
