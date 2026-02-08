#pragma once

#include "dust/async/future.h"
#include "dust/async/result.h"
#include "dust/memory/ref_counted.h"
#include "dust/memory/ref_ptr.h"
#include "llm/chat_message.h"

#include <string>

namespace agent {

class AgentContext;

class ToolCallExecutor final : public dust::RefCounted {
 public:
  ToolCallExecutor();
  ~ToolCallExecutor() override;

  ToolCallExecutor(const ToolCallExecutor&) = delete;
  ToolCallExecutor& operator=(const ToolCallExecutor&) = delete;

  // Sends requests to the LLM and executes tool calls (if any) until an
  // assistant text message is produced.
  dust::FuturePtr<dust::Result<dust::RefPtr<ChatMessage>, std::string>> Send(
      dust::RefPtr<AgentContext> context);
};

}  // namespace agent
