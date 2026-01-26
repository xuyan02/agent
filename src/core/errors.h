#pragma once

#include <string>

namespace agent {

enum class ErrorCode {
  kInvalidArgument,
  kIo,
  kNetwork,
  kPolicyDenied,
  kToolNotFound,
  kLlmError,
  kInternal,
};

} // namespace agent
