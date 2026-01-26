#include "infra/llm/openai_provider.h"

#include "dust/message_loop/message_loop.h"

#include "infra/llm/json_min.h"

#include <cstdlib>
#include <iostream>
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

} // namespace

OpenAIRequest::OpenAIRequest(std::string base_url,
                             std::string api_key,
                             std::string model_name,
                             std::string prompt,
                             OnToken on_token,
                             OnDone on_done)
    : http_(),
      base_url_(std::move(base_url)),
      api_key_(std::move(api_key)),
      model_name_(std::move(model_name)),
      on_token_(std::move(on_token)),
      on_done_(std::move(on_done)) {
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

  // NOTE: minimal JSON; prompt is inserted verbatim (no escaping).
  req.body = std::string("{\"model\":\"") + model_name_ +
             std::string("\",\"stream\":true,\"messages\":[{\"role\":\"user\",\"content\":\"") +
             prompt + std::string("\"}]}");

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
  if (data_line == "[DONE]") return true;

  // Extract first occurrence of "content":"..." within the SSE JSON.
  // This is a minimal parser matching OpenAI streaming responses.
  const std::string kKey = "\"content\":";
  auto p = data_line.find(kKey);
  if (p == std::string::npos) return true;
  p += kKey.size();
  if (p >= data_line.size() || data_line[p] != '"') return true;
  p++;

  std::string out;
  for (; p < data_line.size(); p++) {
    char c = data_line[p];
    if (c == '"') break;
    if (c == '\\') {
      if (p + 1 >= data_line.size()) break;
      char n = data_line[++p];
      out.push_back(n);
      continue;
    }
    out.push_back(c);
  }

  if (!out.empty() && on_token_) on_token_(std::move(out));
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
                                                     std::string prompt,
                                                     LlmRequest::OnToken on_token,
                                                     LlmRequest::OnDone on_done) {
  return std::make_unique<OpenAIRequest>(base_url_, api_key_, std::move(model_name), std::move(prompt),
                                        std::move(on_token), std::move(on_done));
}

} // namespace agent
