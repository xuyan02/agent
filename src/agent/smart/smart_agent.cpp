#include "agent/smart/smart_agent.h"

#include "agent/smart/intuitive_agent.h"
#include "agent/smart/shallow_think_agent.h"
#include "agent/smart/tools/shallow_think_tool.h"

#include <cstdio>
#include <utility>

namespace agent {

SmartAgent::SmartAgent(agent::Runtime* runtime, const agent::AgentContext* ctx)
    : Agent(runtime, ctx),
      intuitive_(std::make_unique<agent::IntuitiveAgent>(runtime, ctx)),
      shallow_(std::make_unique<agent::ShallowThinkAgent>(runtime, ctx)) {
  intuitive_->RegisterTool(std::make_unique<agent::ShallowThinkTool>(this));
}

SmartAgent::~SmartAgent() = default;

void SmartAgent::RunShallowThink(std::string thought,
                                std::string content,
                                dust::OnceFunction<void(std::string answer)> on_done,
                                dust::OnceFunction<void(std::string error)> on_error) {
  if (!runtime() || !ctx()) {
    if (on_error)
      std::move(on_error)("null_dependency");
    return;
  }

  // For now, ShallowThinkAgent only needs the user content; thought can be used later
  // to adjust prompting or logging.
  (void)thought;

  if (!shallow_) {
    if (on_error)
      std::move(on_error)("null_shallow_agent");
    return;
  }

  shallow_->Run(std::move(content), std::move(on_done), std::move(on_error));
}

void SmartAgent::Run(std::string input,
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

  if (!intuitive_) {
    busy_ = false;
    if (pending_on_error_)
      std::move(pending_on_error_)("null_intuitive_agent");
    return;
  }

  std::fprintf(stderr, "[cpp-agent.smart] route: intuitive\n");
  auto* self = this;
  intuitive_->Run(
      std::move(input),
      [self](std::string answer) mutable {
        // IntuitiveAgent now speaks naturally and decides tool usage itself.
        self->busy_ = false;
        if (self->pending_on_done_)
          std::move(self->pending_on_done_)(std::move(answer));
      },
      [self](std::string error) mutable {
        self->busy_ = false;
        if (self->pending_on_error_)
          std::move(self->pending_on_error_)(std::move(error));
      });
}

}  // namespace agent
