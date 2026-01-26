#pragma once

#include <memory>
#include "dust/functional/closure.h"

#include <string>

namespace agent {

// Represents a single in-flight LLM request.
//
// Contract:
// - The factory-created request must establish its underlying connection and
//   send the request during construction.
// - Destruction must disconnect / release underlying resources.
class LlmRequest {
public:
  using OnToken = dust::Function<void(std::string)>;
  using OnDone = dust::OnceFunction<void()>;

  virtual ~LlmRequest() = default;

  LlmRequest(const LlmRequest&) = delete;
  LlmRequest& operator=(const LlmRequest&) = delete;

protected:
  LlmRequest() = default;
};

} // namespace agent
