#include "tool/tool_call_executor.h"

#include "dust/message_loop/message_loop.h"

#include <utility>

namespace agent {

ToolCallExecutor::ToolCallExecutor(agent::ToolProvider* tool_provider,
                                   OnToolCallSuccess on_success,
                                   OnToolCallError on_error,
                                   OnDone on_done)
    : tool_provider_(tool_provider),
      task_runner_(dust::MessageLoop::Current() ? dust::MessageLoop::Current()->task_runner() : nullptr),
      on_success_(std::move(on_success)),
      on_error_(std::move(on_error)),
      on_done_(std::move(on_done)) {}

void ToolCallExecutor::AddToolCalls(std::vector<ToolCall> calls) {
  if (calls.empty()) {
    MaybeDone();
    return;
  }

  pending_ += calls.size();

  for (auto& c : calls) {
    // Copy out small pieces to avoid capturing large vectors.
    const std::string call_id = c.id;
    const std::string function_name = c.name;

    nlohmann::json args = c.arguments;
    if (!args.is_object()) {
      PostError(call_id, "tool_invalid_arguments: " + function_name);
      pending_--;
      PostMaybeDone();
      continue;
    }

    auto* fn = FindFunction(function_name);
    if (!fn) {
      PostError(call_id, "tool_not_found: " + function_name);
      pending_--;
      PostMaybeDone();
      continue;
    }

    const auto weak = weak_.AsWeak();
    fn->InvokeAsync(
        std::move(args),
        [weak, call_id](nlohmann::json out) mutable {
          if (auto* self = weak.Get()) {
            self->PostSuccess(call_id, std::move(out));
            self->pending_--;
            self->PostMaybeDone();
          }
        },
        [weak, call_id](std::string error) mutable {
          if (auto* self = weak.Get()) {
            self->PostError(call_id, std::move(error));
            self->pending_--;
            self->PostMaybeDone();
          }
        });
  }
}

void ToolCallExecutor::Finish() {
  finish_called_ = true;
  PostMaybeDone();
}

Function* ToolCallExecutor::FindFunction(const std::string& function_name) const {
  const auto dot = function_name.find('.');
  if (dot == std::string::npos)
    return nullptr;

  const std::string tool_id = function_name.substr(0, dot);
  auto* tool = tool_provider_ ? tool_provider_->FindTool(tool_id) : nullptr;
  return tool ? tool->FindFunction(function_name) : nullptr;
}

void ToolCallExecutor::PostSuccess(std::string call_id, nlohmann::json out_result) {
  if (!task_runner_) {
    if (on_success_)
      on_success_(std::move(call_id), std::move(out_result));
    return;
  }

  const auto weak = weak_.AsWeak();
  task_runner_->PostTask(dust::OnceClosure([weak, call_id = std::move(call_id), out_result = std::move(out_result)]() mutable {
    if (auto* self = weak.Get()) {
      if (self->on_success_)
        self->on_success_(std::move(call_id), std::move(out_result));
    }
  }));
}

void ToolCallExecutor::PostError(std::string call_id, std::string error) {
  if (!task_runner_) {
    if (on_error_)
      on_error_(std::move(call_id), std::move(error));
    return;
  }

  const auto weak = weak_.AsWeak();
  task_runner_->PostTask(dust::OnceClosure([weak, call_id = std::move(call_id), error = std::move(error)]() mutable {
    if (auto* self = weak.Get()) {
      if (self->on_error_)
        self->on_error_(std::move(call_id), std::move(error));
    }
  }));
}

void ToolCallExecutor::PostMaybeDone() {
  if (!task_runner_) {
    MaybeDone();
    return;
  }

  const auto weak = weak_.AsWeak();
  task_runner_->PostTask(dust::OnceClosure([weak]() mutable {
    if (auto* self = weak.Get())
      self->MaybeDone();
  }));
}

void ToolCallExecutor::MaybeDone() {
  if (!finish_called_ || pending_ != 0 || !on_done_)
    return;

  auto done = std::move(on_done_);
  std::move(done)();
}

}  // namespace agent
