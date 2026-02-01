#pragma once

#include <memory>

#include "dust/functional/closure.h"

#include "llm/llm_message.h"

#include <string>
#include <vector>

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

  // Called exactly once at the end of a round if the round produced tool calls.
  // The tool calls must be complete (arguments parsed and assembled).
  using OnToolCalls = dust::OnceFunction<void(std::vector<ToolCall> tool_calls)>;

  using OnDone = dust::OnceFunction<void()>;

  virtual ~LlmRequest() = default;

  LlmRequest(const LlmRequest&) = delete;
  LlmRequest& operator=(const LlmRequest&) = delete;

protected:
  LlmRequest() = default;
};

} // namespace agent
