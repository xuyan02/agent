#include "infra/http/openai_client.h"

#include "infra/json/json.h"

#include <nlohmann/json.hpp>

#include <curl/curl.h>

#include <iostream>
#include <sstream>
#include <string>

namespace agent {

static size_t write_cb(char* ptr, size_t size, size_t nmemb, void* userdata) {
  auto* s = static_cast<std::string*>(userdata);
  s->append(ptr, size * nmemb);
  return size * nmemb;
}

static std::string json_escape(const std::string& in) {
  // Convert to a properly escaped JSON string (without surrounding quotes).
  const std::string dumped = nlohmann::json(in).dump();
  if (dumped.size() < 2)
    return {};
  return dumped.substr(1, dumped.size() - 2);
}

static const char* role_to_string(agent::LlmRole r) {
  using agent::LlmRole;
  switch (r) {
  case LlmRole::kSystem: return "system";
  case LlmRole::kUser: return "user";
  case LlmRole::kAssistant: return "assistant";
  case LlmRole::kTool: return "tool";
  }
  return "user";
}

static std::string build_request_json(const std::vector<agent::LlmMessage>& messages,
                                     const agent::LlmOptions& options,
                                     const std::string& tools_json) {
  std::ostringstream oss;
  oss << "{\"model\":\"" << json_escape(options.model) << "\",\"temperature\":" << options.temperature
      << ",\"messages\":[";

  bool first = true;
  for (const auto& m : messages) {
    if (!first) oss << ',';
    first = false;

    oss << "{\"role\":\"" << role_to_string(m.role) << "\",";

    if (m.role == agent::LlmRole::kAssistant && !m.tool_calls.empty()) {
      // OpenAI assistant tool-calling schema: {role:"assistant", tool_calls:[...], content:""}
      oss << "\"tool_calls\":[";
      bool first_tc = true;
      for (const auto& tc : m.tool_calls) {
        if (!first_tc) oss << ',';
        first_tc = false;
        oss << "{\"id\":\"" << json_escape(tc.id) << "\",\"type\":\"function\",\"function\":{\"name\":\""
            << json_escape(tc.name) << "\",\"arguments\":\"" << json_escape(tc.arguments_json) << "\"}}";
      }
      oss << "],";
    }

    if (m.role == agent::LlmRole::kTool && m.tool_result) {
      // OpenAI tool message schema: {role:"tool", tool_call_id:"...", content:"..."}
      oss << "\"tool_call_id\":\"" << json_escape(m.tool_result->tool_call_id) << "\",";
    }

    oss << "\"content\":";
    oss << "\"" << json_escape(m.content) << "\"";
    oss << "}";
  }

  oss << "]";

  if (!tools_json.empty()) {
    // Expect tools_json to be a JSON array string.
    oss << ",\"tools\":" << tools_json;
    // Let the model decide when to call tools.
    oss << ",\"tool_choice\":\"auto\"";
  }

  oss << "}";
  return oss.str();
}

static std::string extract_json_string_field_or_empty(const nlohmann::json& obj,
                                                     const std::string& key) {
  if (!obj.is_object())
    return {};
  auto it = obj.find(key);
  if (it == obj.end() || !it->is_string())
    return {};
  return it->get<std::string>();
}

static bool extract_llm_response(const std::string& json_text, agent::LlmResponse* out) {
  *out = agent::LlmResponse{};
  out->assistant_message.role = agent::LlmRole::kAssistant;

  auto root_opt = agent::json::Parse(json_text);
  if (!root_opt)
    return false;

  const auto& root = *root_opt;
  if (!root.is_object())
    return false;

  auto choices_it = root.find("choices");
  if (choices_it == root.end() || !choices_it->is_array() || choices_it->empty())
    return false;

  const auto& choice0 = (*choices_it)[0];
  if (!choice0.is_object())
    return false;

  auto msg_it = choice0.find("message");
  if (msg_it == choice0.end() || !msg_it->is_object())
    return false;

  const auto& msg = *msg_it;
  out->assistant_message.content = extract_json_string_field_or_empty(msg, "content");

  // tool_calls (OpenAI): choices[0].message.tool_calls
  auto tc_it = msg.find("tool_calls");
  if (tc_it != msg.end() && tc_it->is_array()) {
    for (const auto& tc_obj : *tc_it) {
      if (!tc_obj.is_object())
        continue;

      agent::LlmToolCall tc;
      tc.id = extract_json_string_field_or_empty(tc_obj, "id");

      auto fn_it = tc_obj.find("function");
      if (fn_it != tc_obj.end() && fn_it->is_object()) {
        tc.name = extract_json_string_field_or_empty(*fn_it, "name");
        tc.arguments_json = extract_json_string_field_or_empty(*fn_it, "arguments");
      }

      if (!tc.id.empty() && !tc.name.empty()) {
        out->assistant_message.tool_calls.push_back(std::move(tc));
      }
    }
  }

  return true;
}

OpenAIClient::OpenAIClient(std::string base_url, std::string api_key)
    : base_url_(std::move(base_url)), api_key_(std::move(api_key)) {
  if (!base_url_.empty() && base_url_.back() == '/') base_url_.pop_back();
}

void OpenAIClient::set_tools_json(std::string tools_json) {
  tools_json_ = std::move(tools_json);
}

agent::LlmResponse OpenAIClient::Complete(
    const std::vector<agent::LlmMessage>& messages,
    const agent::LlmOptions& options) {
  if (api_key_.empty()) {
    return {};
  }

  auto url = base_url_ + "/chat/completions";
  auto body = build_request_json(messages, options, tools_json_);

  auto clip = [](const std::string& s, size_t max_len) {
    if (s.size() <= max_len) return s;
    return s.substr(0, max_len) + "\n...TRUNCATED...";
  };

  if (log_requests_) {
    std::cout << "----- LLM HTTP REQUEST -----\n";
    std::cout << "POST " << url << "\n";
    std::cout << "BODY:\n";
    std::cout << clip(body, 64 * 1024) << "\n";
    std::cout << "----- END LLM HTTP REQUEST -----\n";
  }

  CURL* curl = curl_easy_init();
  if (!curl) {
    return {};
  }

  std::string response;
  struct curl_slist* headers = nullptr;
  headers = curl_slist_append(headers, "Content-Type: application/json");
  std::string auth = "Authorization: Bearer " + api_key_;
  headers = curl_slist_append(headers, auth.c_str());

  curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
  curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
  curl_easy_setopt(curl, CURLOPT_POST, 1L);
  curl_easy_setopt(curl, CURLOPT_POSTFIELDS, body.c_str());
  curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_cb);
  curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response);

  CURLcode res = curl_easy_perform(curl);
  long http_code = 0;
  curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);

  curl_slist_free_all(headers);
  curl_easy_cleanup(curl);

  if (log_requests_) {
    std::cout << "----- LLM HTTP RESPONSE -----\n";
    std::cout << "HTTP " << http_code << "\n";
    std::cout << "BODY:\n";
    std::cout << clip(response, 64 * 1024) << "\n";
    std::cout << "----- END LLM HTTP RESPONSE -----\n";
  }

  if (res != CURLE_OK) {
    return {};
  }
  if (http_code < 200 || http_code >= 300) {
    return {};
  }

  agent::LlmResponse parsed;
  if (!extract_llm_response(response, &parsed)) return {};
  return parsed;
}

}  // namespace agent
