#pragma once

#include "tool/function.h"
#include "tool/tool_call.h"
#include "tool/tool_provider.h"

#include "dust/functional/function.h"
#include "dust/memory/weak_ptr.h"
#include "dust/task_runner/task_runner.h"

#include <string>
#include <utility>
#include <vector>

namespace agent {

// Executes tool calls in parallel.
//
// Finish semantics:
// - Call AddToolCalls() any number of times.
// - Call Finish() once to signal no more calls will be added.
// - OnDone is fired when Finish() has been called AND all tool calls that were
//   added before Finish() have completed.
class ToolCallExecutor {
 public:
  using OnToolCallSuccess = dust::Function<void(std::string call_id, nlohmann::json out_result)>;
  using OnToolCallError = dust::Function<void(std::string call_id, std::string error)>;
  using OnDone = dust::OnceFunction<void()>;

  ToolCallExecutor(agent::ToolProvider* tool_provider,
                   OnToolCallSuccess on_success,
                   OnToolCallError on_error,
                   OnDone on_done);

  ToolCallExecutor(const ToolCallExecutor&) = delete;
  ToolCallExecutor& operator=(const ToolCallExecutor&) = delete;

  void AddToolCalls(std::vector<ToolCall> calls);

  void Finish();

 private:
  Function* FindFunction(const std::string& function_name) const;

  void PostSuccess(std::string call_id, nlohmann::json out_result);
  void PostError(std::string call_id, std::string error);
  void PostMaybeDone();

  void MaybeDone();

  dust::SupportsWeakPtr<ToolCallExecutor> weak_{this};

  agent::ToolProvider* tool_provider_{nullptr};
  dust::RefPtr<dust::TaskRunner> task_runner_;

  OnToolCallSuccess on_success_;
  OnToolCallError on_error_;
  OnDone on_done_;

  bool finish_called_{false};
  size_t pending_{0};
};

}  // namespace agent
