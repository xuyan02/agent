#include "llm/openai/openai_sender.h"

#include "http/http_client.h"
#include "http/http_types.h"
#include "json/json.h"
#include "tool/tool.h"


#include <nlohmann/json.hpp>

#include <cstdlib>
#include <optional>
#include <utility>

namespace agent {
namespace {

using SenderResult = dust::Result<dust::RefPtr<ChatMessage>>;

bool DebugOpenAi() {
  const char* v = std::getenv("CPP_AGENT_DEBUG_OPENAI");
  return v && *v && std::strcmp(v, "0") != 0;
}

nlohmann::json ToJsonSchema(const TypeSpec& t);

nlohmann::json FieldsToJsonSchema(const std::vector<FieldSpec>& fields) {
  nlohmann::json properties = nlohmann::json::object();
  nlohmann::json required = nlohmann::json::array();

  for (const auto& p : fields) {
    if (!p.type())
      continue;

    nlohmann::json p_schema = ToJsonSchema(p.type());
    if (!p.description().empty())
      p_schema["description"] = p.description();

    properties[p.name()] = std::move(p_schema);
    if (p.required())
      required.push_back(p.name());
  }

  nlohmann::json schema;
  schema["type"] = "object";
  schema["properties"] = std::move(properties);
  if (!required.empty())
    schema["required"] = std::move(required);
  schema["additionalProperties"] = false;
  return schema;
}

nlohmann::json ToJsonSchema(const TypeSpec& t) {
  nlohmann::json schema;

  switch (t.kind()) {
    case TypeSpec::Kind::kString: {
      schema["type"] = "string";
      if (const auto* s = t.AsString(); s && !s->enum_values.empty())
        schema["enum"] = s->enum_values;
      return schema;
    }

    case TypeSpec::Kind::kNumber:
      schema["type"] = "number";
      return schema;

    case TypeSpec::Kind::kInteger:
      schema["type"] = "integer";
      return schema;

    case TypeSpec::Kind::kBoolean:
      schema["type"] = "boolean";
      return schema;

    case TypeSpec::Kind::kObject: {
      const auto* o = t.AsObject();
      if (!o)
        return nlohmann::json::object();
      schema = FieldsToJsonSchema(o->properties);
      schema["additionalProperties"] = o->additional_properties;
      return schema;
    }

    case TypeSpec::Kind::kArray: {
      schema["type"] = "array";
      const auto* a = t.AsArray();
      if (a && a->items)
        schema["items"] = ToJsonSchema(a->items);
      return schema;
    }
  }

  return nlohmann::json::object();
}

std::optional<nlohmann::json> BuildToolsJson(const std::vector<Tool*>& tools) {
  if (tools.empty())
    return std::nullopt;

  nlohmann::json out = nlohmann::json::array();
  for (const Tool* tool : tools) {
    if (!tool)
      continue;

    const ToolSpec* tool_spec = tool->GetSpec();
    if (!tool_spec)
      continue;
    for (const auto& fn_spec : tool_spec->functions()) {
      nlohmann::json tool_obj;
      tool_obj["type"] = "function";

      nlohmann::json fn_obj;
      fn_obj["name"] = fn_spec.name();
      fn_obj["description"] = fn_spec.description();
      fn_obj["parameters"] = FieldsToJsonSchema(fn_spec.params());

      tool_obj["function"] = std::move(fn_obj);
      out.push_back(std::move(tool_obj));
    }
  }

  return out;
}

nlohmann::json BuildMessagesJson(const std::vector<dust::RefPtr<ChatMessage>>& messages) {
  nlohmann::json out = nlohmann::json::array();
  for (const auto& m : messages) {
    if (!m)
      continue;

    nlohmann::json msg;
    switch (m->role()) {
      case ChatRole::kSystem:
        msg["role"] = "system";
        break;
      case ChatRole::kUser:
        msg["role"] = "user";
        break;
      case ChatRole::kAssistant:
        msg["role"] = "assistant";
        break;
      case ChatRole::kTool:
        msg["role"] = "tool";
        break;
    }

    const ChatContent* c = m->content();
    if (!c)
      continue;

    if (c->kind() == ChatContent::Kind::kText) {
      const auto* t = static_cast<const ChatContent::Text*>(c);
      msg["content"] = t->text();
    } else if (c->kind() == ChatContent::Kind::kToolResult) {
      const auto* tr = static_cast<const ChatContent::ToolResult*>(c);
      // OpenAI tool message expects content string; encode json as string.
      msg["tool_call_id"] = tr->tool_call_id();
      msg["content"] = json::Dump(tr->result());
    } else if (c->kind() == ChatContent::Kind::kToolCalls) {
      const auto* tc = static_cast<const ChatContent::ToolCalls*>(c);
      // OpenAI assistant tool call message: role=assistant + tool_calls array.
      msg["tool_calls"] = tc->tool_calls();
    } else {
      continue;
    }

    out.push_back(std::move(msg));
  }
  return out;
}

SenderResult ParseChatCompletionsResponse(const nlohmann::json& root) {
  if (!root.is_object())
    return SenderResult::Err("openai: response is not an object");

  if (!root.contains("choices") || !root["choices"].is_array() || root["choices"].empty()) {
    return SenderResult::Err("openai: missing choices");
  }

  const auto& choice0 = root["choices"][0];
  if (!choice0.is_object() || !choice0.contains("message") || !choice0["message"].is_object()) {
    return SenderResult::Err("openai: missing choices[0].message");
  }

  const auto& msg = choice0["message"];

  if (msg.contains("tool_calls") && msg["tool_calls"].is_array() && !msg["tool_calls"].empty()) {
    // Preserve provider-native payload; higher layers can map into Function/Tool later.
    return SenderResult::Ok(
        ChatMessage::CreateToolCalls(ChatRole::kAssistant, msg["tool_calls"]));
  }

  if (msg.contains("content") && msg["content"].is_string()) {
    return SenderResult::Ok(
        ChatMessage::CreateText(ChatRole::kAssistant, msg["content"].get<std::string>()));
  }

  return SenderResult::Err("openai: message has neither tool_calls nor string content");
}

}  // namespace

OpenAiSender::OpenAiSender(std::string base_url, std::string api_key, std::string model)
    : LlmSender(std::move(model)), base_url_(std::move(base_url)), api_key_(std::move(api_key)) {}

OpenAiSender::~OpenAiSender() = default;

dust::FuturePtr<dust::Result<dust::RefPtr<ChatMessage>, std::string>> OpenAiSender::Send(
    std::vector<dust::RefPtr<ChatMessage>> messages,
    std::vector<Tool*> tools) {
  // Pseudo async flow (C++-ish):
  // {
  //   HttpRequest req;
  //   req.method = "POST";
  //   req.url = base_url_ + "/v1/chat/completions";
  //   req.headers = {
  //     {"Authorization", "Bearer " + api_key_},
  //     {"Content-Type", "application/json"},
  //   };
  //
  //   if (DebugOpenAi())
  //     std::fprintf(stderr, "[cpp-agent.openai] POST %s\n", req.url.c_str());
  //
  //   http_ = std::make_unique<HttpClient>();
  //
  //   nlohmann::json body;
  //   body["model"] = model();
  //   body["stream"] = false;
  //   body["messages"] = BuildMessagesJson(messages);
  //   if (auto tools_json = BuildToolsJson(tools)) {
  //     body["tools"] = *tools_json;
  //     body["tool_choice"] = "auto";
  //   }
  //   req.body = json::Dump(body);
  //
  //   auto resp_or_err = await#1 http_->Send(std::move(req));
  //   if (!resp_or_err.ok())
  //     return Err("openai: http error: " + resp_or_err.error().message);
  //
  //   HttpResponse resp = std::move(resp_or_err.value());
  //   if (resp.status < 200 || resp.status >= 300)
  //     return Err("openai: http status=" + std::to_string(resp.status));
  //
  //   auto parsed = json::Parse(resp.body);
  //   if (!parsed)
  //     return Err("openai: failed to parse response JSON");
  //
  //   SenderResult msg_or_err = ParseChatCompletionsResponse(*parsed);
  //   if (!msg_or_err.ok())
  //     return Err(std::move(msg_or_err.error()));
  //
  //   return Ok(std::move(msg_or_err.value()));
  // }

  class SendFuture final : public dust::Future<dust::Result<dust::RefPtr<ChatMessage>, std::string>> {
   public:
    SendFuture(std::string base_url,
               std::string api_key,
               std::string model,
               std::vector<dust::RefPtr<ChatMessage>> messages,
               std::vector<Tool*> tools)
        : base_url_(std::move(base_url)),
          api_key_(std::move(api_key)),
          model_(std::move(model)),
          messages_(std::move(messages)),
          tools_(std::move(tools)) {}

    dust::Poll<dust::Result<dust::RefPtr<ChatMessage>, std::string>> PollOnce(
        dust::PollContext& ctx) override {
      if (DebugOpenAi())
        std::fprintf(stderr, "[cpp-agent.openai] Send PollOnce state=%d\n", static_cast<int>(state_));

      switch (state_) {
        case State::kInit: {
          HttpRequest req;
          req.method = "POST";
          req.url = base_url_ + "/v1/chat/completions";
          req.headers.push_back({"Authorization", "Bearer " + api_key_});
          req.headers.push_back({"Content-Type", "application/json"});

          nlohmann::json body;
          body["model"] = model_;
          body["stream"] = false;
          body["messages"] = BuildMessagesJson(messages_);

          if (auto tools_json = BuildToolsJson(tools_)) {
            body["tools"] = *tools_json;
            body["tool_choice"] = "auto";
          }

          req.body = json::Dump(body);

          if (DebugOpenAi()) {
            bool has_auth = false;
            size_t auth_len = 0;
            for (const auto& h : req.headers) {
              if (h.name == "Authorization") {
                has_auth = true;
                auth_len = h.value.size();
              }
            }

            std::fprintf(stderr,
                         "[cpp-agent.openai] POST %s (auth=%d auth_len=%zu)\n",
                         req.url.c_str(),
                         has_auth ? 1 : 0,
                         auth_len);

            std::fprintf(stderr,
                         "[cpp-agent.openai] request_body=%s\n",
                         req.body.c_str());
          }

          http_ = std::make_unique<HttpClient>();
          http_future_ = http_->Send(std::move(req));
          if (DebugOpenAi()) {
            std::fprintf(stderr, "[cpp-agent.openai] http_future created=%d\n", http_future_ ? 1 : 0);
          }

          // Immediately poll await#1 once in the same tick to ensure IO is armed.
          state_ = State::kAwait1;
          [[fallthrough]];
        }

        case State::kAwait1: {
          if (DebugOpenAi())
            std::fprintf(stderr, "[cpp-agent.openai] await#1: http->Send (http_future=%d)\n", http_future_ ? 1 : 0);

          if (!http_future_) {
            state_ = State::kDone;
            done_ = dust::Result<dust::RefPtr<ChatMessage>, std::string>::Err(
                "openai: internal error (missing http future)");
            return dust::Poll<dust::Result<dust::RefPtr<ChatMessage>, std::string>>::Ready(
                std::move(done_));
          }

          auto polled = http_future_->PollOnce(ctx);
          if (polled.is_pending()) {
            if (DebugOpenAi())
              std::fprintf(stderr, "[cpp-agent.openai] await#1 -> Pending\n");
            return dust::Poll<dust::Result<dust::RefPtr<ChatMessage>, std::string>>::Pending();
          }

          if (DebugOpenAi())
            std::fprintf(stderr, "[cpp-agent.openai] await#1 -> Ready\n");

          state_ = State::kDone;
          return dust::Poll<dust::Result<dust::RefPtr<ChatMessage>, std::string>>::Ready(
              Finish(std::move(polled).TakeReady()));
        }

        case State::kDone:
          return dust::Poll<dust::Result<dust::RefPtr<ChatMessage>, std::string>>::Ready(
              std::move(done_));
      }

      state_ = State::kDone;
      done_ = dust::Result<dust::RefPtr<ChatMessage>, std::string>::Err(
          "openai: internal error (bad state)");
      return dust::Poll<dust::Result<dust::RefPtr<ChatMessage>, std::string>>::Ready(
          std::move(done_));
    }

   private:
    enum class State { kInit, kAwait1, kDone };

    dust::Result<dust::RefPtr<ChatMessage>, std::string> Finish(
        dust::Result<HttpResponse, HttpError> r) {
      http_future_ = nullptr;
      http_.reset();

      if (!r.ok()) {
        done_ = dust::Result<dust::RefPtr<ChatMessage>, std::string>::Err(
            "openai: http error: " + r.error().message);
        return std::move(done_);
      }

      HttpResponse resp = std::move(r.value());

      if (resp.status < 200 || resp.status >= 300) {
        if (DebugOpenAi()) {
          std::string snippet = resp.body;
          constexpr size_t kMax = 2048;
          if (snippet.size() > kMax)
            snippet.resize(kMax);
          std::fprintf(stderr,
                       "[cpp-agent.openai] non-2xx: status=%ld body_snippet=%s\n",
                       resp.status,
                       snippet.c_str());
        }

        done_ = dust::Result<dust::RefPtr<ChatMessage>, std::string>::Err(
            "openai: http status=" + std::to_string(resp.status));
        return std::move(done_);
      }

      auto parsed = json::Parse(resp.body);
      if (!parsed) {
        done_ = dust::Result<dust::RefPtr<ChatMessage>, std::string>::Err(
            "openai: failed to parse response JSON");
        return std::move(done_);
      }

      SenderResult result = ParseChatCompletionsResponse(*parsed);
      if (!result.ok()) {
        done_ = dust::Result<dust::RefPtr<ChatMessage>, std::string>::Err(std::move(result.error()));
        return std::move(done_);
      }

      done_ = dust::Result<dust::RefPtr<ChatMessage>, std::string>::Ok(std::move(result.value()));
      return std::move(done_);
    }

    State state_ = State::kInit;
    dust::Result<dust::RefPtr<ChatMessage>, std::string> done_ =
        dust::Result<dust::RefPtr<ChatMessage>, std::string>::Err("not started");

    std::string base_url_;
    std::string api_key_;
    std::string model_;
    std::vector<dust::RefPtr<ChatMessage>> messages_;
    std::vector<Tool*> tools_;

    std::unique_ptr<HttpClient> http_;
    dust::FuturePtr<dust::Result<HttpResponse, HttpError>> http_future_;
  };

  return dust::MakeRefPtr<SendFuture>(base_url_, api_key_, model(), std::move(messages),
                                     std::move(tools));
}

}  // namespace agent
