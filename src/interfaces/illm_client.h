#pragma once

#include "core/tool_protocol.h"

#include <string>
#include <vector>

namespace cpp_agent::interfaces {

struct LlmOptions {
  std::string model;
  double temperature{0.2};
};

struct LlmResponse {
  cpp_agent::core::Message assistant_message;
};

class ILlmClient {
public:
  virtual ~ILlmClient() = default;
  virtual LlmResponse complete(const std::vector<cpp_agent::core::Message>& messages,
                               const LlmOptions& options) = 0;
};

} // namespace cpp_agent::interfaces
