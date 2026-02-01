#include "agent/one_step_agent.h"

#include <iostream>
#include <utility>

namespace agent {

OneStepAgent::OneStepAgent(agent::Runtime* runtime, const agent::AgentContext* ctx)
    : Agent(runtime), ctx_(ctx) {}

void OneStepAgent::Run(std::string input,
                       dust::OnceFunction<void(std::string answer)> on_done,
                       dust::OnceFunction<void(std::string error)> on_error) {
  if (busy_) {
    on_error("busy");
    return;
  }
  busy_ = true;

  if (!runtime() || !ctx_) {
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

void OneStepAgent::StartRequest() {
  assistant_msg_ = agent::LlmMessage{};
  assistant_msg_.role = agent::LlmRole::kAssistant;

  std::vector<agent::LlmMessage> messages;
  messages.push_back(
      agent::LlmMessage{.role = agent::LlmRole::kSystem, .content = ctx_->GetSystemPrompt()});
  for (const auto& m : history_)
    messages.push_back(m);

  auto tools = ctx_->GetTools();

  auto on_token = [this](std::string delta) { assistant_msg_.content += delta; };

  auto on_tool_calls = [this](std::vector<agent::LlmToolCall> tool_calls) mutable {
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

  req_ = runtime()->CreateRequest(ctx_->GetModelName(), std::move(messages), std::move(tools),
                                  std::move(on_token), std::move(on_tool_calls),
                                  std::move(on_done_req));
  if (!req_) {
    busy_ = false;
    if (pending_on_error_)
      std::move(pending_on_error_)("failed_to_create_llm_request");
    return;
  }
}

void OneStepAgent::OnToolCalls(std::vector<agent::LlmToolCall> tool_calls) {
  req_.reset();
  ExecuteToolCalls(/*index=*/0, std::move(tool_calls));
}

void OneStepAgent::ExecuteToolCalls(size_t index, std::vector<agent::LlmToolCall> tool_calls) {
  if (index >= tool_calls.size()) {
    StartRequest();
    return;
  }

  const auto tc = tool_calls[index];
  auto* fn = runtime()->FindFunction(tc.name);
  if (!fn) {
    busy_ = false;
    if (pending_on_error_)
      std::move(pending_on_error_)("tool_not_found: " + tc.name);
    return;
  }

  fn->InvokeAsync(
      tc.arguments_json, [this, tool_call_id = tc.id, index, tool_calls = std::move(tool_calls)](
                             std::string out_result_json, std::string out_error) mutable {
        agent::LlmMessage tool_msg;
        tool_msg.role = agent::LlmRole::kTool;
        tool_msg.tool_result = agent::LlmToolResult{
            .tool_call_id = tool_call_id,
            .content = out_error.empty() ? out_result_json
                                         : std::string{"{\"error\":\""} + out_error + "\"}"};
        history_.push_back(std::move(tool_msg));

        ExecuteToolCalls(index + 1, std::move(tool_calls));
      });
}

}  // namespace agent
