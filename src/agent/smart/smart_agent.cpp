#include "agent/smart/smart_agent.h"

#include "agent/smart/intuitive_agent.h"
#include "agent/smart/routing_result_parse.h"
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
  auto input_shared = std::make_shared<std::string>(std::move(input));
  intuitive_->Run(
      *input_shared,
      [self, input_shared](std::string answer_json) mutable {
        auto rr = agent::ParseRoutingResultFromJson(answer_json);
        if (!rr) {
          self->busy_ = false;
          if (self->pending_on_error_)
            std::move(self->pending_on_error_)("invalid_json_from_intuitive_agent");
          return;
        }

        if (rr->outcome == agent::RoutingResult::Outcome::kAnswer) {
          std::fprintf(stderr, "[cpp-agent.smart] intuitive -> outcome=answer answer.len=%zu",
                       rr->content.size());
          if (!rr->reason.empty())
            std::fprintf(stderr, " reason=%s", rr->reason.c_str());
          std::fprintf(stderr, "\n");
          self->busy_ = false;
          if (self->pending_on_done_)
            std::move(self->pending_on_done_)(rr->content);
          return;
        }

        if (rr->outcome == agent::RoutingResult::Outcome::kDeep) {
          std::fprintf(stderr, "[cpp-agent.smart] intuitive -> outcome=deep");
          if (!rr->reason.empty())
            std::fprintf(stderr, " reason=%s", rr->reason.c_str());
          std::fprintf(stderr, "\n");
          self->busy_ = false;
          if (self->pending_on_done_)
            std::move(self->pending_on_done_)(
                "This request was routed to deep thinking, but DeepThinkAgent is not implemented yet.");
          return;
        }

        std::fprintf(stderr, "[cpp-agent.smart] intuitive -> outcome=shallow");
        if (!rr->reason.empty())
          std::fprintf(stderr, " reason=%s", rr->reason.c_str());
        std::fprintf(stderr, "\n");
        std::fprintf(stderr, "[cpp-agent.smart] route: shallow\n");
        if (!self->shallow_) {
          self->busy_ = false;
          if (self->pending_on_error_)
            std::move(self->pending_on_error_)("null_shallow_agent");
          return;
        }

        self->shallow_->Run(
            std::move(*input_shared),
            [self](std::string shallow_json) mutable {
              auto rr2 = agent::ParseRoutingResultFromJson(shallow_json);
              if (!rr2) {
                self->busy_ = false;
                if (self->pending_on_error_)
                  std::move(self->pending_on_error_)("invalid_json_from_shallow_agent");
                return;
              }

              if (rr2->outcome == agent::RoutingResult::Outcome::kAnswer) {
                std::fprintf(stderr, "[cpp-agent.smart] shallow -> outcome=answer answer.len=%zu",
                             rr2->content.size());
                if (!rr2->reason.empty())
                  std::fprintf(stderr, " reason=%s", rr2->reason.c_str());
                std::fprintf(stderr, "\n");
                self->busy_ = false;
                if (self->pending_on_done_)
                  std::move(self->pending_on_done_)(rr2->content);
                return;
              }

              std::fprintf(stderr, "[cpp-agent.smart] shallow -> outcome=deep");
              if (!rr2->reason.empty())
                std::fprintf(stderr, " reason=%s", rr2->reason.c_str());
              std::fprintf(stderr, "\n");
              self->busy_ = false;
              if (self->pending_on_done_)
                std::move(self->pending_on_done_)(
                    "This request was routed to deep thinking, but DeepThinkAgent is not implemented yet.");
            },
            [self](std::string error) mutable {
              self->busy_ = false;
              if (self->pending_on_error_)
                std::move(self->pending_on_error_)(std::move(error));
            });
      },
      [this](std::string error) mutable {
        busy_ = false;
        if (pending_on_error_)
          std::move(pending_on_error_)(std::move(error));
      });
}

}  // namespace agent
