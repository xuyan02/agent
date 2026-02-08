#include "agent/in_memory_history.h"

#include <string>
#include <utility>

#include "dust/async/just.h"

namespace agent {

InMemoryHistory::InMemoryHistory() = default;

InMemoryHistory::~InMemoryHistory() = default;

dust::FuturePtr<std::vector<dust::RefPtr<ChatMessage>>> InMemoryHistory::GetAll(
    dust::RefPtr<AgentContext>) {
  return dust::Just(messages_);
}

dust::FuturePtr<dust::Result<void, std::string>> InMemoryHistory::Append(
    dust::RefPtr<AgentContext>,
    dust::RefPtr<ChatMessage> message) {
  if (!message)
    return dust::Just(dust::Result<void, std::string>::Err("history: null message"));

  messages_.push_back(std::move(message));
  return dust::Just(dust::Result<void, std::string>::Ok());
}

dust::RefPtr<ChatMessage> InMemoryHistory::GetLast(dust::RefPtr<AgentContext>) {
  if (messages_.empty())
    return nullptr;
  return messages_.back();
}

}  // namespace agent
