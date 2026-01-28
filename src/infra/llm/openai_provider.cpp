#include "infra/llm/openai_provider.h"

#include "dust/message_loop/message_loop.h"

#include "infra/json/json.h"
#include "infra/llm/openai_stream_accumulator.h"

#include <nlohmann/json.hpp>

#include <cstdlib>
#include <iostream>
#include <sstream>
#include <string.h>
#include <utility>

namespace agent {
namespace {

bool DebugLlm() {
  const char* v = std::getenv("CPP_AGENT_DEBUG_LLM");
  return v && *v && strcmp(v, "0") != 0;
}


bool contains_model(const std::vector<std::string>& models, const std::string& model_name) {
  for (const auto& m : models) {
    if (m == model_name) return true;
  }
  return false;
}

static std::string JsonEscapeString(const std::string& s) {
  // Convert to a properly escaped JSON string (without surrounding quotes).
  // nlohmann::json handles control chars and UTF-8 safely.
  const std::string dumped = nlohmann::json(s).dump();
  if (dumped.size() < 2) return {};
  return dumped.substr(1, dumped.size() - 2);
}

} // namespace

OpenAIRequest::OpenAIRequest(std::string base_url,
                             std::string api_key,
                             std::string model_name,
                             std::vector<LlmMessage> messages,
                             std::vector<agent::Tool> tools,
                             OnToken on_token,
                             OnToolCalls on_tool_calls,
                             OnDone on_done)
    : http_(),
      base_url_(std::move(base_url)),
      api_key_(std::move(api_key)),
      model_name_(std::move(model_name)),
      on_token_(std::move(on_token)),
      on_tool_calls_(std::move(on_tool_calls)),
      on_done_(std::move(on_done)) {
  acc_.Reset();
  if (DebugLlm()) {
    std::cerr << "[cpp-agent.llm] OpenAIRequest ctor base_url=" << base_url_ << " model="
              << model_name_ << std::endl;
  }
  http::Request req;
  req.method = "POST";
  req.url = base_url_ + "/v1/chat/completions";
  req.headers.push_back({"Content-Type", "application/json"});
  req.headers.push_back({"Accept", "text/event-stream"});
  req.headers.push_back({"Authorization", "Bearer " + api_key_});

  std::string tools_json;
  if (!tools.empty()) {
    // Expand Tool->Functions into OpenAI chat.completions tools=[{type:function,function:{...}}].
    std::ostringstream oss;
    oss << ",\"tools\":[";

    bool first = true;
    for (const auto& tool : tools) {
      for (const auto& fn : tool.functions) {
        if (!fn) continue;
        const auto& spec = fn->spec();
        if (!first) oss << ',';
        first = false;
        oss << "{\"type\":\"function\",\"function\":{\"name\":\"" << JsonEscapeString(spec.name)
            << "\",\"description\":\"" << JsonEscapeString(spec.description) << "\",\"parameters\":"
            << spec.parameters_json << "}}";
      }
    }
    oss << ']';
    tools_json = oss.str();
  }

  std::ostringstream msg_oss;
  msg_oss << "\"messages\":[";

  auto role_to_string = [](LlmRole r) -> const char* {
    switch (r) {
    case LlmRole::kSystem: return "system";
    case LlmRole::kUser: return "user";
    case LlmRole::kAssistant: return "assistant";
    case LlmRole::kTool: return "tool";
    }
    return "user";
  };

  bool first_msg = true;
  for (const auto& m : messages) {
    if (!first_msg) msg_oss << ',';
    first_msg = false;

    msg_oss << "{\"role\":\"" << role_to_string(m.role) << "\"";

    // tool message
    if (m.role == LlmRole::kTool && m.tool_result.has_value()) {
      msg_oss << ",\"tool_call_id\":\"" << JsonEscapeString(m.tool_result->tool_call_id) << "\"";
    }

    msg_oss << ",\"content\":\"" << JsonEscapeString(m.content) << "\"";

    // assistant tool_calls
    if (m.role == LlmRole::kAssistant && !m.tool_calls.empty()) {
      msg_oss << ",\"tool_calls\":[";
      bool first_tc = true;
      for (const auto& tc : m.tool_calls) {
        if (!first_tc) msg_oss << ',';
        first_tc = false;
        msg_oss << "{\"id\":\"" << JsonEscapeString(tc.id)
                << "\",\"type\":\"function\",\"function\":{\"name\":\""
                << JsonEscapeString(tc.name) << "\",\"arguments\":";

        // Arguments must be a JSON string value in OpenAI schema.
        msg_oss << "\"" << JsonEscapeString(tc.arguments_json) << "\"";
        msg_oss << "}}";
      }
      msg_oss << ']';
    }

    msg_oss << '}';
  }
  msg_oss << ']';

  req.body = std::string("{\"model\":\"") + model_name_ +
             std::string("\",\"stream\":true,") + msg_oss.str() + tools_json +
             std::string("}");

  req.on_body_chunk = [this](const char* data, size_t n) {
    sse_.Feed(data, n);
    for (auto& ev : sse_.PopEvents()) {
      if (!HandleSseDataLine(ev)) return false;
    }
    return true;
  };

  call_ = http_.Start(std::move(req), [this](http::Result r) {
    if (DebugLlm()) {
      std::cerr << "[cpp-agent.llm] OpenAIRequest done status=" << r.response.status
                << " error_code=" << static_cast<int>(r.error.code) << " curl=" << r.error.curl_code
                << " msg='" << r.error.message << "'" << std::endl;
    }
    if (on_done_) std::move(on_done_)();
  });

  if (DebugLlm()) {
    std::cerr << "[cpp-agent.llm] AsyncClient::Start issued" << std::endl;
  }
}

OpenAIRequest::~OpenAIRequest() = default;

bool OpenAIRequest::HandleSseDataLine(const std::string& data_line) {
  OpenAIStreamDelta d;
  if (!acc_.FeedDataLine(data_line, &d)) {
    std::cerr << "[cpp-agent.llm] error: failed to parse SSE data line\n";
    return false;
  }

  if (DebugLlm()) {
    const bool maybe_has_tool_calls = data_line.find("\"tool_calls\"") != std::string::npos;
    const bool maybe_has_finish_reason = data_line.find("\"finish_reason\"") != std::string::npos;
    if (!d.content_delta.empty() || maybe_has_tool_calls || maybe_has_finish_reason || data_line == "[DONE]") {
      std::cerr << "[cpp-agent.llm] sse delta content.len=" << d.content_delta.size()
                << " finish=" << (d.has_finish_reason ? 1 : 0)
                << " saw.tool_calls=" << (maybe_has_tool_calls ? 1 : 0) << "\n";
    }
  }

  if (!d.content_delta.empty() && on_token_) {
    on_token_(std::move(d.content_delta));
  }

  if (d.has_finish_reason) {
    const bool has_tools = acc_.HasToolCalls();
    if (DebugLlm()) {
      std::cerr << "[cpp-agent.llm] round finished has_tool_calls=" << (has_tools ? 1 : 0)
                << " on_tool_calls=" << (on_tool_calls_ ? 1 : 0) << "\n";

      if (!has_tools) {
        const bool maybe_has_tool_calls = data_line.find("\"tool_calls\"") != std::string::npos;
        if (maybe_has_tool_calls) {
          std::string s = data_line;
          if (s.size() > 2048) s = s.substr(0, 2048) + "...(truncated)";
          std::cerr << "[cpp-agent.llm] raw(tool_calls) " << s << "\n";
        }
      }
    }
    if (has_tools && on_tool_calls_) {
      auto msg = acc_.BuildAssistantMessage();
      if (DebugLlm()) {
        std::cerr << "[cpp-agent.llm] tool_calls n=" << msg.tool_calls.size() << "\n";
        for (size_t i = 0; i < msg.tool_calls.size(); i++) {
          const auto& tc = msg.tool_calls[i];
          std::cerr << "[cpp-agent.llm]  tc[" << i << "] id=" << tc.id << " name=" << tc.name
                    << " args.len=" << tc.arguments_json.size() << "\n";
        }
      }
      std::move(on_tool_calls_)(std::move(msg.tool_calls));
    }
    return true;
  }

  return true;
}

OpenAIProvider::OpenAIProvider(std::string name,
                               std::vector<std::string> models,
                               std::string base_url,
                               std::string api_key)
    : name_(std::move(name)),
      models_(std::move(models)),
      base_url_(std::move(base_url)),
      api_key_(std::move(api_key)) {}

bool OpenAIProvider::SupportsModel(const std::string& model_name) const {
  return contains_model(models_, model_name);
}

std::unique_ptr<LlmRequest> OpenAIProvider::Create(std::string model_name,
                                                 std::vector<LlmMessage> messages,
                                                 std::vector<agent::Tool> tools,
                                                 LlmRequest::OnToken on_token,
                                                 LlmRequest::OnToolCalls on_tool_calls,
                                                 LlmRequest::OnDone on_done) {
  return std::make_unique<OpenAIRequest>(base_url_,
                                        api_key_,
                                        std::move(model_name),
                                        std::move(messages),
                                        std::move(tools),
                                        std::move(on_token),
                                        std::move(on_tool_calls),
                                        std::move(on_done));
}

} // namespace agent
