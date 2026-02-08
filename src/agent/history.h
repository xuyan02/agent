#pragma once

#include "dust/async/future.h"
#include "dust/memory/ref_counted.h"
#include "dust/memory/ref_ptr.h"
#include "llm/chat_message.h"

#include <string>
#include <vector>

namespace agent {

class AgentContext;

class History : public dust::RefCounted {
 public:
  virtual ~History() = default;

  virtual dust::FuturePtr<std::vector<dust::RefPtr<ChatMessage>>> GetAll(
      dust::RefPtr<AgentContext> context) = 0;

  virtual dust::FuturePtr<dust::Result<void, std::string>> Append(
      dust::RefPtr<AgentContext> context,
      dust::RefPtr<ChatMessage> message) = 0;

  virtual dust::RefPtr<ChatMessage> GetLast(dust::RefPtr<AgentContext> context) = 0;

 protected:
  History() = default;
};

}  // namespace agent
