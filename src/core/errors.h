#pragma once

#include <stdexcept>
#include <string>

namespace cpp_agent::core {

enum class ErrorCode {
  kInvalidArgument,
  kIo,
  kNetwork,
  kPolicyDenied,
  kToolNotFound,
  kLlmError,
  kInternal,
};

class AgentError final : public std::runtime_error {
public:
  AgentError(ErrorCode code, std::string message)
      : std::runtime_error(message), code_(code), message_(std::move(message)) {}

  [[nodiscard]] ErrorCode code() const noexcept { return code_; }
  [[nodiscard]] const std::string& message() const noexcept { return message_; }

private:
  ErrorCode code_;
  std::string message_;
};

} // namespace cpp_agent::core
