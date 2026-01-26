#pragma once

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

} // namespace cpp_agent::core
