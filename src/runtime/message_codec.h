#pragma once

#include "runtime/message.h"

#include <deque>
#include <string>
#include <vector>

namespace agent {

// Converts a batch of messages (all addressed to the same agent) into a single
// LLM input string:
//   @from: content\n
//
// This function drains |q|.
std::string BuildAgentBatchInput(std::deque<Message>* q);

// Parses multi-target output produced by an agent:
//   @to: first line\n
//   continuation\n
// into per-target Message blocks.
//
// - Continuation lines before any @to: are dropped.
// - Invalid @ header lines are dropped.
// - |from| is filled into each Message.
std::vector<Message> ParseAgentMultiTargetOutput(const std::string& from,
                                                const std::string& text);

} // namespace agent
