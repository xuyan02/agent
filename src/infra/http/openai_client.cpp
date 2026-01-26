#include "infra/http/openai_client.h"

#include "core/status.h"

#include <curl/curl.h>

#include <iostream>
#include <sstream>
#include <string>

namespace cpp_agent::infra::http {

static size_t write_cb(char* ptr, size_t size, size_t nmemb, void* userdata) {
  auto* s = static_cast<std::string*>(userdata);
  s->append(ptr, size * nmemb);
  return size * nmemb;
}

static std::string json_escape(const std::string& in) {
  std::string out;
  out.reserve(in.size() + 16);
  for (char c : in) {
    switch (c) {
    case '\\': out += "\\\\"; break;
    case '"': out += "\\\""; break;
    case '\n': out += "\\n"; break;
    case '\r': out += "\\r"; break;
    case '\t': out += "\\t"; break;
    default: out.push_back(c); break;
    }
  }
  return out;
}

static const char* role_to_string(cpp_agent::core::Role r) {
  using cpp_agent::core::Role;
  switch (r) {
  case Role::kSystem: return "system";
  case Role::kUser: return "user";
  case Role::kAssistant: return "assistant";
  case Role::kTool: return "tool";
  }
  return "user";
}

static std::string build_request_json(const std::vector<cpp_agent::core::Message>& messages,
                                     const cpp_agent::interfaces::LlmOptions& options,
                                     const std::string& tools_json) {
  std::ostringstream oss;
  oss << "{\"model\":\"" << json_escape(options.model) << "\",\"temperature\":" << options.temperature
      << ",\"messages\":[";

  bool first = true;
  for (const auto& m : messages) {
    if (!first) oss << ',';
    first = false;

    oss << "{\"role\":\"" << role_to_string(m.role) << "\",";

    if (m.role == cpp_agent::core::Role::kAssistant && !m.tool_calls.empty()) {
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

    if (m.role == cpp_agent::core::Role::kTool && m.tool_result) {
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

static std::string extract_json_string_field_or_empty(const std::string& json,
                                                     const std::string& key,
                                                     size_t start_pos = 0) {
  auto pos = json.find('"' + key + '"', start_pos);
  if (pos == std::string::npos) return {};
  pos = json.find(':', pos);
  if (pos == std::string::npos) return {};
  pos++;
  while (pos < json.size() && (json[pos] == ' ' || json[pos] == '\n' || json[pos] == '\r' || json[pos] == '\t')) pos++;
  if (pos >= json.size() || json[pos] != '"') return {};
  pos++;
  std::string out;
  for (; pos < json.size(); ++pos) {
    char c = json[pos];
    if (c == '\\') {
      if (pos + 1 >= json.size()) break;
      char n = json[pos + 1];
      if (n == 'n') out.push_back('\n');
      else if (n == 'r') out.push_back('\r');
      else if (n == 't') out.push_back('\t');
      else out.push_back(n);
      pos++;
      continue;
    }
    if (c == '"') break;
    out.push_back(c);
  }
  return out;
}

static size_t skip_ws(const std::string& json, size_t i) {
  while (i < json.size() && (json[i] == ' ' || json[i] == '\n' || json[i] == '\r' || json[i] == '\t')) i++;
  return i;
}

static size_t skip_json_string(const std::string& json, size_t i) {
  // i points to opening '"'
  if (i >= json.size() || json[i] != '"') return i;
  i++;
  while (i < json.size()) {
    if (json[i] == '\\') {
      i += 2;
      continue;
    }
    if (json[i] == '"') return i + 1;
    i++;
  }
  return i;
}

static size_t skip_json_value(const std::string& json, size_t i);

static size_t skip_json_array(const std::string& json, size_t i) {
  if (i >= json.size() || json[i] != '[') return i;
  i++;
  for (;;) {
    i = skip_ws(json, i);
    if (i >= json.size()) return i;
    if (json[i] == ']') return i + 1;
    i = skip_json_value(json, i);
    i = skip_ws(json, i);
    if (i < json.size() && json[i] == ',') i++;
  }
}

static size_t skip_json_object(const std::string& json, size_t i) {
  if (i >= json.size() || json[i] != '{') return i;
  i++;
  for (;;) {
    i = skip_ws(json, i);
    if (i >= json.size()) return i;
    if (json[i] == '}') return i + 1;
    i = skip_json_string(json, i);
    i = skip_ws(json, i);
    if (i < json.size() && json[i] == ':') i++;
    i = skip_ws(json, i);
    i = skip_json_value(json, i);
    i = skip_ws(json, i);
    if (i < json.size() && json[i] == ',') i++;
  }
}

static size_t skip_json_value(const std::string& json, size_t i) {
  i = skip_ws(json, i);
  if (i >= json.size()) return i;
  char c = json[i];
  if (c == '"') return skip_json_string(json, i);
  if (c == '{') return skip_json_object(json, i);
  if (c == '[') return skip_json_array(json, i);
  // number, true, false, null
  while (i < json.size() && json[i] != ',' && json[i] != ']' && json[i] != '}' && json[i] != '\n' && json[i] != '\r') i++;
  return i;
}

static std::string extract_raw_json_field_or_empty(const std::string& json,
                                                  const std::string& key,
                                                  size_t start_pos = 0) {
  auto pos = json.find('"' + key + '"', start_pos);
  if (pos == std::string::npos) return {};
  pos = json.find(':', pos);
  if (pos == std::string::npos) return {};
  pos++;
  pos = skip_ws(json, pos);
  if (pos >= json.size()) return {};

  size_t end = skip_json_value(json, pos);
  if (end <= pos || end > json.size()) return {};
  return json.substr(pos, end - pos);
}

static cpp_agent::core::Result<cpp_agent::interfaces::LlmResponse> extract_llm_response_r(const std::string& json) {
  cpp_agent::interfaces::LlmResponse out;
  out.assistant_message.role = cpp_agent::core::Role::kAssistant;

  // Very small parser: locate first message object under choices[0].message.
  auto msg_pos = json.find("\"message\"");
  if (msg_pos == std::string::npos) {
    return cpp_agent::core::Status::Error(cpp_agent::core::ErrorCode::kLlmError,
                                         "LLM response missing message");
  }

  // content
  out.assistant_message.content = extract_json_string_field_or_empty(json, "content", msg_pos);

  // tool_calls (OpenAI): choices[0].message.tool_calls
  auto tool_calls_raw = extract_raw_json_field_or_empty(json, "tool_calls", msg_pos);
  if (!tool_calls_raw.empty() && tool_calls_raw.front() == '[') {
    // Parse each element as object by scanning.
    size_t i = 0;
    i++; // skip '['
    for (;;) {
      i = skip_ws(tool_calls_raw, i);
      if (i >= tool_calls_raw.size() || tool_calls_raw[i] == ']') break;
      if (tool_calls_raw[i] != '{') {
        i++;
        continue;
      }
      size_t obj_end = skip_json_object(tool_calls_raw, i);
      if (obj_end <= i || obj_end > tool_calls_raw.size()) break;
      std::string obj = tool_calls_raw.substr(i, obj_end - i);

      cpp_agent::core::ToolCall tc;
      tc.id = extract_json_string_field_or_empty(obj, "id");
      // name is in function.name; arguments in function.arguments
      auto fn_raw = extract_raw_json_field_or_empty(obj, "function");
      if (!fn_raw.empty() && fn_raw.front() == '{') {
        tc.name = extract_json_string_field_or_empty(fn_raw, "name");
        tc.arguments_json = extract_json_string_field_or_empty(fn_raw, "arguments");
        // arguments is a JSON-string; unescape minimal backslash sequences already handled in extract_json_string
      }

      if (!tc.id.empty() && !tc.name.empty()) {
        out.assistant_message.tool_calls.push_back(std::move(tc));
      }

      i = obj_end;
      i = skip_ws(tool_calls_raw, i);
      if (i < tool_calls_raw.size() && tool_calls_raw[i] == ',') i++;
    }
  }

  return out;
}

OpenAIClient::OpenAIClient(std::string base_url, std::string api_key)
    : base_url_(std::move(base_url)), api_key_(std::move(api_key)) {
  if (!base_url_.empty() && base_url_.back() == '/') base_url_.pop_back();
}

void OpenAIClient::set_tools_json(std::string tools_json) {
  tools_json_ = std::move(tools_json);
}

cpp_agent::interfaces::LlmResponse OpenAIClient::complete(
    const std::vector<cpp_agent::core::Message>& messages,
    const cpp_agent::interfaces::LlmOptions& options) {
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

  auto parsed_r = extract_llm_response_r(response);
  if (!parsed_r.ok()) return {};
  return std::move(parsed_r.value());
}

} // namespace cpp_agent::infra::http
