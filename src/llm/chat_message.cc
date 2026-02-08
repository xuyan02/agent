#include "llm/chat_message.h"

#include <utility>

namespace agent {

dust::RefPtr<ChatMessage> ChatMessage::CreateText(ChatRole role, std::string text) {
  return dust::MakeRefPtr<ChatMessage>(role,
                                      std::make_unique<ChatContent::Text>(std::move(text)));
}

dust::RefPtr<ChatMessage> ChatMessage::CreateToolCalls(ChatRole role,
                                                       nlohmann::json tool_calls) {
  return dust::MakeRefPtr<ChatMessage>(
      role, std::make_unique<ChatContent::ToolCalls>(std::move(tool_calls)));
}

dust::RefPtr<ChatMessage> ChatMessage::CreateToolResult(ChatRole role,
                                                        std::string tool_call_id,
                                                        nlohmann::json result) {
  return dust::MakeRefPtr<ChatMessage>(
      role,
      std::make_unique<ChatContent::ToolResult>(std::move(tool_call_id), std::move(result)));
}

}  // namespace agent
