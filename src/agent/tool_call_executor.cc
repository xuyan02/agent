#include "agent/tool_call_executor.h"

#include "agent/agent_context.h"
#include "agent/history.h"
#include "agent/session.h"
#include "llm/llm_sender.h"
#include "tool/tool.h"

#include "json/json.h"

#include "dust/async/just.h"

#include <cstdio>
#include <cstdlib>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace agent {

namespace {

bool DebugToolLoop() {
  const char* v = std::getenv("CPP_AGENT_DEBUG_TOOL_LOOP");
  return v && v[0] != 0;
}

using SendResult = dust::Result<dust::RefPtr<ChatMessage>, std::string>;

struct ParsedToolCall {
  std::string id;
  std::string function_name;
  nlohmann::json args;
};

SendResult Err(std::string s) { return SendResult::Err(std::move(s)); }

dust::Result<void, std::string> ErrVoid(std::string s) {
  return dust::Result<void, std::string>::Err(std::move(s));
}

dust::Result<void, std::string> ParseToolCallsJson(const nlohmann::json& tool_calls,
                                                   std::vector<ParsedToolCall>* out) {
  if (!out)
    return ErrVoid("tool_loop: internal error");
  out->clear();

  if (!tool_calls.is_array())
    return ErrVoid("tool_loop: tool_calls is not array");

  for (const auto& tc : tool_calls) {
    if (!tc.is_object())
      return ErrVoid("tool_loop: tool_call is not object");

    if (!tc.contains("id") || !tc["id"].is_string())
      return ErrVoid("tool_loop: tool_call missing id");

    if (!tc.contains("type") || !tc["type"].is_string() || tc["type"] != "function")
      return ErrVoid("tool_loop: tool_call type is not function");

    if (!tc.contains("function") || !tc["function"].is_object())
      return ErrVoid("tool_loop: tool_call missing function");

    const auto& fn = tc["function"];
    if (!fn.contains("name") || !fn["name"].is_string())
      return ErrVoid("tool_loop: tool_call missing function.name");

    if (!fn.contains("arguments") || !fn["arguments"].is_string())
      return ErrVoid("tool_loop: tool_call missing function.arguments");

    ParsedToolCall c;
    c.id = tc["id"].get<std::string>();
    c.function_name = fn["name"].get<std::string>();

    // OpenAI returns arguments as a JSON string.
    const std::string args_str = fn["arguments"].get<std::string>();
    auto args = json::Parse(args_str);
    if (!args)
      return ErrVoid("tool_loop: failed to parse function.arguments");

    c.args = std::move(*args);
    out->push_back(std::move(c));
  }

  return dust::Result<void, std::string>::Ok();
}

Tool* FindToolForFunction(const std::vector<dust::RefPtr<Tool>>& tools,
                          const std::string& function_name) {
  for (const auto& t : tools) {
    if (!t)
      continue;

    const ToolSpec* spec = t->GetSpec();
    if (!spec)
      continue;

    for (const auto& fn : spec->functions()) {
      if (fn.name() == function_name)
        return t.Get();
    }
  }
  return nullptr;
}

}  // namespace

ToolCallExecutor::ToolCallExecutor() = default;

ToolCallExecutor::~ToolCallExecutor() = default;

dust::FuturePtr<dust::Result<dust::RefPtr<ChatMessage>, std::string>> ToolCallExecutor::Send(
    dust::RefPtr<AgentContext> context) {
  class SendFuture final : public dust::Future<SendResult> {
   public:
    explicit SendFuture(dust::RefPtr<AgentContext> context) : context_(std::move(context)) {}

    // Pseudo-code ("C++ + await"):
    // {
    //   while (true) {
    //     auto messages = await#1 history->GetAll(ctx);
    //     auto reply = await#2 sender->Send(messages, tools);
    //     await#3 history->Append(ctx, reply);
    //
    //     if (reply is Text)
    //       return Ok(reply);
    //
    //     // reply is ToolCalls
    //     for each tool_call in reply.tool_calls in parallel {
    //       json result = await#4 tool->Invoke(ctx, name, args);
    //       await#5 history->Append(ctx, ToolResult(id, result));
    //     }
    //     // after all tool calls appended, continue loop to send tool results
    //   }
    // }

    dust::Poll<SendResult> PollOnce(dust::PollContext& ctx) override {
      dust::PollContext child_ctx(ctx.waker());

      while (state_ != State::kDone) {
        switch (state_) {
          case State::kInit: {
            if (!context_ || !context_->session() || !context_->history()) {
              done_ = Err("tool_loop: missing context/session/history");
              state_ = State::kDone;
              break;
            }

            model_ = context_->session()->default_model();
            if (model_.empty()) {
              done_ = Err("tool_loop: missing default model");
              state_ = State::kDone;
              break;
            }

            history_ = context_->history();
            state_ = State::kGetAll;
            break;
          }

          case State::kGetAll: {
            if (DebugToolLoop())
              std::fprintf(stderr, "[cpp-agent.tool_loop] state=kGetAll\n");

            if (!get_all_)
              get_all_ = history_->GetAll(context_);

            if (!get_all_) {
              done_ = Err("tool_loop: history GetAll returned null future");
              state_ = State::kDone;
              break;
            }

            auto polled = get_all_->PollOnce(child_ctx);
            if (polled.is_pending())
              return dust::Poll<SendResult>::Pending();

            messages_ = polled.TakeReady();
            get_all_ = nullptr;

            if (DebugToolLoop())
              std::fprintf(stderr,
                           "[cpp-agent.tool_loop] state=kGetAll ready messages=%zu\n",
                           messages_.size());

            // Tools list is sourced from context (may differ from session tools in future).
            tools_ = context_->tools();

            sender_ = context_->session()->CreateSender(model_);
            if (!sender_) {
              done_ = Err("tool_loop: failed to create sender");
              state_ = State::kDone;
              break;
            }

            std::vector<Tool*> tool_ptrs;
            tool_ptrs.reserve(tools_.size());
            for (const auto& t : tools_)
              tool_ptrs.push_back(t.Get());

            send_ = sender_->Send(std::move(messages_), std::move(tool_ptrs));
            if (!send_) {
              done_ = Err("tool_loop: sender Send returned null future");
              state_ = State::kDone;
              break;
            }

            state_ = State::kAwaitReply;
            break;
          }

          case State::kAwaitReply: {
            if (DebugToolLoop())
              std::fprintf(stderr, "[cpp-agent.tool_loop] state=kAwaitReply\n");

            auto polled = send_->PollOnce(child_ctx);
            if (polled.is_pending())
              return dust::Poll<SendResult>::Pending();

            auto r = polled.TakeReady();
            send_ = nullptr;
            sender_.reset();

            if (!r.ok()) {
              done_ = Err(r.error());
              state_ = State::kDone;
              break;
            }

            reply_ = std::move(r.value());
            if (!reply_ || !reply_->content()) {
              done_ = Err("tool_loop: sender returned null reply");
              state_ = State::kDone;
              break;
            }

            if (reply_->role() != ChatRole::kAssistant) {
              done_ = Err("tool_loop: reply role is not assistant");
              state_ = State::kDone;
              break;
            }

            append_reply_ = history_->Append(context_, reply_);
            if (!append_reply_) {
              done_ = Err("tool_loop: history Append(reply) returned null future");
              state_ = State::kDone;
              break;
            }

            state_ = State::kAppendReply;
            break;
          }

          case State::kAppendReply: {
            if (DebugToolLoop())
              std::fprintf(stderr, "[cpp-agent.tool_loop] state=kAppendReply\n");

            auto polled = append_reply_->PollOnce(child_ctx);
            if (polled.is_pending())
              return dust::Poll<SendResult>::Pending();

            auto r = polled.TakeReady();
            append_reply_ = nullptr;
            if (!r.ok()) {
              done_ = Err(r.error());
              state_ = State::kDone;
              break;
            }

            const ChatContent* c = reply_->content();
            if (DebugToolLoop())
              std::fprintf(stderr,
                           "[cpp-agent.tool_loop] appended reply kind=%d\n",
                           static_cast<int>(c->kind()));

            if (c->kind() == ChatContent::Kind::kText) {
              done_ = SendResult::Ok(std::move(reply_));
              state_ = State::kDone;
              break;
            }

            if (c->kind() != ChatContent::Kind::kToolCalls) {
              done_ = Err("tool_loop: assistant reply is neither text nor tool_calls");
              state_ = State::kDone;
              break;
            }

            const auto* tc = static_cast<const ChatContent::ToolCalls*>(c);
            parsed_calls_.clear();
            auto parse_r = ParseToolCallsJson(tc->tool_calls(), &parsed_calls_);
            if (!parse_r.ok()) {
              done_ = Err(parse_r.error());
              state_ = State::kDone;
              break;
            }

            if (parsed_calls_.empty()) {
              done_ = Err("tool_loop: empty tool_calls");
              state_ = State::kDone;
              break;
            }

            tool_futures_.clear();
            tool_done_.clear();
            tool_appends_.clear();
            tool_results_.clear();

            tool_futures_.reserve(parsed_calls_.size());
            tool_done_.assign(parsed_calls_.size(), false);
            tool_appends_.assign(parsed_calls_.size(), nullptr);
            tool_results_.assign(parsed_calls_.size(), nullptr);

            for (size_t i = 0; i < parsed_calls_.size(); ++i) {
              const auto& call = parsed_calls_[i];
              Tool* tool = FindToolForFunction(tools_, call.function_name);
              if (!tool) {
                done_ = Err("tool_loop: no tool for function: " + call.function_name);
                state_ = State::kDone;
                break;
              }

              // Tool::Invoke itself is infallible; nullptr means function not found.
              dust::FuturePtr<nlohmann::json> f = tool->Invoke(context_, call.function_name, call.args);
              if (!f) {
                done_ = Err("tool_loop: tool missing function: " + call.function_name);
                state_ = State::kDone;
                break;
              }
              tool_futures_.push_back(std::move(f));
            }

            if (state_ == State::kDone)
              break;

            if (DebugToolLoop())
              std::fprintf(stderr,
                           "[cpp-agent.tool_loop] executing %zu tool calls\n",
                           parsed_calls_.size());

            state_ = State::kAwaitTools;
            break;
          }

          case State::kAwaitTools: {
            // Drive tool invokes + per-tool append futures until all tool results are appended.
            // This does not rely on Wake() for correctness (tests may manually poll).
            size_t spin = 0;
            while (true) {
              bool progressed = false;
              bool any_pending = false;

              if (DebugToolLoop()) {
                if ((spin++ % 1024) == 0) {
                  size_t remaining = 0;
                  for (size_t i = 0; i < tool_done_.size(); ++i) {
                    if (!tool_done_[i])
                      ++remaining;
                  }
                  std::fprintf(stderr,
                               "[cpp-agent.tool_loop] kAwaitTools spin=%zu remaining=%zu\n",
                               spin,
                               remaining);
                }
              }

              for (size_t i = 0; i < tool_futures_.size(); ++i) {
                if (tool_done_[i])
                  continue;

                // If we already have an append future in-flight for this tool result,
                // drive it first.
                if (tool_appends_[i]) {
                  auto ap = tool_appends_[i]->PollOnce(child_ctx);
                  if (ap.is_pending()) {
                    any_pending = true;
                    continue;
                  }

                  auto ar = ap.TakeReady();
                  tool_appends_[i] = nullptr;
                  if (!ar.ok()) {
                    done_ = Err(ar.error());
                    state_ = State::kDone;
                    break;
                  }

                  tool_done_[i] = true;
                  progressed = true;
                  continue;
                }

                auto polled = tool_futures_[i]->PollOnce(child_ctx);
                if (polled.is_pending()) {
                  any_pending = true;
                  continue;
                }

                // Tool invocation completed. Append result immediately.
                nlohmann::json result = polled.TakeReady();
                tool_futures_[i] = nullptr;

                const auto& call = parsed_calls_[i];
                tool_results_[i] = ChatMessage::CreateToolResult(ChatRole::kTool,
                                                                call.id,
                                                                std::move(result));
                tool_appends_[i] = history_->Append(context_, tool_results_[i]);
                if (!tool_appends_[i]) {
                  done_ = Err("tool_loop: history Append(tool_result) returned null future");
                  state_ = State::kDone;
                  break;
                }

                any_pending = true;
                progressed = true;
              }

              if (state_ == State::kDone)
                break;

              bool all_done = true;
              for (size_t i = 0; i < tool_done_.size(); ++i) {
                if (!tool_done_[i]) {
                  all_done = false;
                  break;
                }
              }

              if (all_done) {
                // All tool results appended, loop back to ask the model again.
                reply_ = nullptr;
                parsed_calls_.clear();
                state_ = State::kGetAll;
                break;
              }

              // If we made progress, spin once more in the same PollOnce tick to
              // drain other ready completions.
              if (progressed)
                continue;

              // No progress possible in this tick.
              if (any_pending)
                return dust::Poll<SendResult>::Pending();

              // Should be unreachable (not all_done but also not pending).
              done_ = Err("tool_loop: internal error (tools stalled)");
              state_ = State::kDone;
              break;
            }
            break;
          }

          case State::kDone:
            break;
        }
      }

      return dust::Poll<SendResult>::Ready(std::move(done_));
    }

   private:
    enum class State {
      kInit,
      kGetAll,
      kAwaitReply,
      kAppendReply,
      kAwaitTools,
      kDone,
    };

    dust::RefPtr<AgentContext> context_;
    dust::RefPtr<History> history_;

    std::string model_;

    dust::FuturePtr<std::vector<dust::RefPtr<ChatMessage>>> get_all_;
    std::vector<dust::RefPtr<ChatMessage>> messages_;

    std::vector<dust::RefPtr<Tool>> tools_;
    std::unique_ptr<LlmSender> sender_;
    dust::FuturePtr<dust::Result<dust::RefPtr<ChatMessage>, std::string>> send_;

    dust::RefPtr<ChatMessage> reply_;
    dust::FuturePtr<dust::Result<void, std::string>> append_reply_;

    std::vector<ParsedToolCall> parsed_calls_;
    std::vector<dust::FuturePtr<nlohmann::json>> tool_futures_;
    std::vector<bool> tool_done_;
    std::vector<dust::FuturePtr<dust::Result<void, std::string>>> tool_appends_;
    std::vector<dust::RefPtr<ChatMessage>> tool_results_;

    SendResult done_ = SendResult::Err("tool_loop: not started");
    State state_ = State::kInit;
  };

  return dust::MakeRefPtr<SendFuture>(std::move(context));
}

}  // namespace agent
