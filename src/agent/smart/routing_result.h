#pragma once

#include <string>

namespace agent {

// Common routing result used by SmartAgent and its sub-agents.
struct RoutingResult {
  enum class Outcome {
    kAnswer,
    kShallow,
    kDeep,
  };

  Outcome outcome{Outcome::kAnswer};

  // For Outcome::kAnswer, this is the final user-facing answer.
  // For Outcome::kShallow / kDeep, this must be empty.
  std::string content;

  // Optional internal reason, not shown to the user.
  std::string reason;
};

}  // namespace agent
