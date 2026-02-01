#include "agent/simple_agent.h"

#include "infra/json/json.h"

#include <iostream>
#include <utility>

namespace agent {

SimpleAgent::SimpleAgent(agent::Runtime* runtime, const agent::AgentContext* ctx)
    : Agent(runtime, ctx) {}

void SimpleAgent::Run(std::string input,
                     dust::OnceFunction<void(std::string answer)> on_done,
                     dust::OnceFunction<void(std::string error)> on_error) {
  if (busy_) {
    on_error("busy");
    return;
  }
  busy_ = true;

  if (!runtime() || !ctx()) {
    busy_ = false;
    on_error("null_dependency");
    return;
  }

  pending_on_done_ = std::move(on_done);
  pending_on_error_ = std::move(on_error);

  // Append user message to history.
  history_.push_back(agent::LlmMessage{.role = agent::LlmRole::kUser, .content = std::move(input)});

  StartRequest();
}

void SimpleAgent::StartRequest() {
  assistant_msg_ = agent::LlmMessage{};
  assistant_msg_.role = agent::LlmRole::kAssistant;

  std::vector<agent::LlmMessage> messages;
  messages.push_back(
      agent::LlmMessage{.role = agent::LlmRole::kSystem, .content = ctx()->GetSystemPrompt()});
  for (const auto& m : history_)
    messages.push_back(m);

  auto tool_owners = ctx()->GetTools();
  std::vector<agent::Tool*> tools;
  tools.reserve(tool_owners.size());
  for (auto& t : tool_owners) {
    if (!t)
      continue;

    const std::string id = t->id;
    RegisterTool(std::move(t));
    tools.push_back(FindTool(id));
  }

  auto on_token = [this](std::string delta) { assistant_msg_.content += delta; };

  auto on_tool_calls = [this](std::vector<agent::ToolCall> tool_calls) mutable {
    // Persist the assistant tool call request to history.
    agent::LlmMessage m;
    m.role = agent::LlmRole::kAssistant;
    m.tool_calls = std::move(tool_calls);
    history_.push_back(std::move(m));

    OnToolCalls(history_.back().tool_calls);
  };

  auto on_done_req = [this]() mutable {
    req_.reset();
    busy_ = false;

    if (!pending_on_done_ || !pending_on_error_) {
      // Callbacks already consumed. Ignore.
      return;
    }

    // Treat a synthesized error token as an error.
    if (assistant_msg_.content.rfind("[cpp-agent.error] ", 0) == 0) {
      std::move(pending_on_error_)(assistant_msg_.content);
      return;
    }

    // Persist final assistant content to history.
    agent::LlmMessage m;
    m.role = agent::LlmRole::kAssistant;
    m.content = assistant_msg_.content;
    history_.push_back(std::move(m));

    std::move(pending_on_done_)(assistant_msg_.content);
  };

  req_ = runtime()->CreateRequest(ctx()->GetModelName(), std::move(messages), std::move(tools),
                                  std::move(on_token), std::move(on_tool_calls),
                                  std::move(on_done_req));
  if (!req_) {
    busy_ = false;
    if (pending_on_error_)
      std::move(pending_on_error_)("failed_to_create_llm_request");
    return;
  }
}

void SimpleAgent::OnToolCalls(std::vector<agent::ToolCall> tool_calls) {
  req_.reset();

  std::vector<agent::ToolCall> calls = std::move(tool_calls);

  auto* self = this;

  tool_call_executor_ = std::make_unique<agent::ToolCallExecutor>(
      this,
      [self](std::string call_id, nlohmann::json out) {
        agent::LlmMessage tool_msg;
        tool_msg.role = agent::LlmRole::kTool;
        tool_msg.tool_result =
            agent::LlmToolResult{.tool_call_id = std::move(call_id), .content = agent::json::Dump(out)};
        self->history_.push_back(std::move(tool_msg));
      },
      [self](std::string call_id, std::string error) {
        agent::LlmMessage tool_msg;
        tool_msg.role = agent::LlmRole::kTool;
        nlohmann::json err;
        err["error"] = std::move(error);
        tool_msg.tool_result =
            agent::LlmToolResult{.tool_call_id = std::move(call_id), .content = agent::json::Dump(err)};
        self->history_.push_back(std::move(tool_msg));
      },
      [self]() mutable {
        self->tool_call_executor_->Finish();
        self->tool_call_executor_.reset();
        self->StartRequest();
      });

  tool_call_executor_->AddToolCalls(std::move(calls));
}

}  // namespace agent
