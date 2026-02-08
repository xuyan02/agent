#pragma once


#include "dust/memory/ref_counted.h"
#include "dust/memory/ref_ptr.h"

#include <nlohmann/json.hpp>

#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace agent {

enum class ChatRole {
  kSystem,
  kUser,
  kAssistant,
  kTool,
};

class ChatContent {
 public:
  enum class Kind {
    kText,
    kToolCalls,
    kToolResult,
  };

  class Text;
  class ToolCalls;
  class ToolResult;

  virtual ~ChatContent() = default;

  virtual Kind kind() const = 0;

};

class ChatContent::Text final : public ChatContent {
 public:
  explicit Text(std::string text) : text_(std::move(text)) {}

  Kind kind() const override { return Kind::kText; }

  const std::string& text() const { return text_; }

 private:
  std::string text_;
};

class ChatContent::ToolCalls final : public ChatContent {
 public:
  explicit ToolCalls(nlohmann::json tool_calls) : tool_calls_(std::move(tool_calls)) {}

  Kind kind() const override { return Kind::kToolCalls; }

  // Provider-specific tool calls payload.
  const nlohmann::json& tool_calls() const { return tool_calls_; }

 private:
  nlohmann::json tool_calls_;
};

class ChatContent::ToolResult final : public ChatContent {
 public:
  ToolResult(std::string tool_call_id, nlohmann::json result)
      : tool_call_id_(std::move(tool_call_id)), result_(std::move(result)) {}

  Kind kind() const override { return Kind::kToolResult; }

  const std::string& tool_call_id() const { return tool_call_id_; }
  const nlohmann::json& result() const { return result_; }

 private:
  std::string tool_call_id_;
  nlohmann::json result_;
};

class ChatMessage : public dust::RefCounted {
 public:
  static dust::RefPtr<ChatMessage> CreateText(ChatRole role, std::string text);

  static dust::RefPtr<ChatMessage> CreateToolCalls(ChatRole role, nlohmann::json tool_calls);

  static dust::RefPtr<ChatMessage> CreateToolResult(ChatRole role,
                                                   std::string tool_call_id,
                                                   nlohmann::json result);

  ChatRole role() const { return role_; }
  const ChatContent* content() const { return content_.get(); }

 public:
  ChatMessage(ChatRole role, std::unique_ptr<ChatContent> content)
      : role_(role), content_(std::move(content)) {}

 private:

  ChatRole role_{ChatRole::kUser};
  std::unique_ptr<ChatContent> content_;
};

}  // namespace agent
