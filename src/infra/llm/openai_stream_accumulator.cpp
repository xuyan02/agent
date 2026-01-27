#include "infra/llm/openai_stream_accumulator.h"

#include "infra/llm/json_min.h"

#include <cstddef>
#include <cstdlib>

namespace agent {

void OpenAIStreamAccumulator::Reset() {
  content_.clear();
  tool_calls_.clear();
}

void OpenAIStreamAccumulator::EnsureToolIndex(size_t idx) {
  while (tool_calls_.size() <= idx) tool_calls_.push_back({});
}

static std::string extract_delta_obj(const std::string& data_line) {
  // Very small helper: find first "\"delta\":" and return the following JSON object.
  const std::string k = "\"delta\":";
  auto p = data_line.find(k);
  if (p == std::string::npos) return {};
  p += k.size();
  p = skip_ws(data_line, p);
  if (p >= data_line.size() || data_line[p] != '{') return {};
  const size_t end = skip_json_object(data_line, p);
  if (end <= p || end > data_line.size()) return {};
  return data_line.substr(p, end - p);
}

static std::string extract_finish_reason(const std::string& data_line) {
  // Find first "\"finish_reason\":" and return string value or "null".
  const std::string k = "\"finish_reason\":";
  auto p = data_line.find(k);
  if (p == std::string::npos) return {};
  p += k.size();
  p = skip_ws(data_line, p);
  if (p >= data_line.size()) return {};
  if (data_line.compare(p, 4, "null") == 0) return "null";
  if (data_line[p] != '"') return {};
  p++;
  std::string out;
  for (; p < data_line.size(); p++) {
    char c = data_line[p];
    if (c == '"') break;
    if (c == '\\') {
      if (p + 1 >= data_line.size()) break;
      char e = data_line[++p];
      out.push_back('\\');
      out.push_back(e);
      continue;
    }
    out.push_back(c);
  }
  return json_unescape(out);
}

bool OpenAIStreamAccumulator::FeedDataLine(const std::string& data_line, OpenAIStreamDelta* out_delta) {
  if (out_delta) *out_delta = {};
  if (data_line == "[DONE]") {
    if (out_delta) out_delta->has_finish_reason = true;
    return true;
  }

  // content delta
  // OpenAI streaming typically contains: choices[0].delta.content
  const std::string kContent = "\"content\":";
  auto pc = data_line.find(kContent);
  if (pc != std::string::npos) {
    pc += kContent.size();
    if (pc < data_line.size() && data_line[pc] == '"') {
      pc++;
      std::string raw;
      for (; pc < data_line.size(); pc++) {
        char c = data_line[pc];
        if (c == '"') break;
        if (c == '\\') {
          if (pc + 1 >= data_line.size()) break;
          char e = data_line[++pc];
          raw.push_back('\\');
          raw.push_back(e);
          continue;
        }
        raw.push_back(c);
      }
      std::string tok = json_unescape(raw);
      content_ += tok;
      if (out_delta) out_delta->content_delta = tok;
    }
  }

  // finish_reason (if present and non-null it signals end of round)
  const std::string fr = extract_finish_reason(data_line);
  if (!fr.empty() && fr != "null") {
    if (out_delta) out_delta->has_finish_reason = true;
  }

  // tool_calls delta (best-effort): parse delta.tool_calls array of objects.
  const std::string delta_obj = extract_delta_obj(data_line);
  if (!delta_obj.empty()) {
    std::string tool_calls_raw;
    if (extract_raw_field(delta_obj, "tool_calls", &tool_calls_raw) && !tool_calls_raw.empty() &&
        tool_calls_raw.front() == '[') {
      std::vector<std::string> objs;
      if (split_top_level_objects(tool_calls_raw, &objs)) {
        for (const auto& obj : objs) {
          // index
          std::string idx_raw;
          size_t idx = 0;
          if (extract_raw_field(obj, "index", &idx_raw)) {
            // idx_raw is a number token
            idx = static_cast<size_t>(std::strtoul(idx_raw.c_str(), nullptr, 10));
          }
          EnsureToolIndex(idx);

          // id (may appear only once)
          std::string id = extract_json_string_or_empty(obj, "id");
          if (!id.empty()) tool_calls_[idx].id = std::move(id);

          // function object
          std::string fn_raw;
          if (extract_raw_field(obj, "function", &fn_raw) && !fn_raw.empty() && fn_raw.front() == '{') {
            const std::string name = extract_json_string_or_empty(fn_raw, "name");
            if (!name.empty()) tool_calls_[idx].name = name;

            const std::string args = extract_json_string_or_empty(fn_raw, "arguments");
            if (!args.empty()) {
              tool_calls_[idx].arguments_json += args;
              if (out_delta) {
                LlmToolCall tc;
                tc.id = tool_calls_[idx].id;
                tc.name = tool_calls_[idx].name;
                tc.arguments_json = args;
                out_delta->tool_calls_delta.push_back(std::move(tc));
              }
            }
          }
        }
      }
    }
  }

  return true;
}

bool OpenAIStreamAccumulator::HasToolCalls() const {
  for (const auto& tc : tool_calls_) {
    if (!tc.name.empty() || !tc.id.empty() || !tc.arguments_json.empty()) return true;
  }
  return false;
}

LlmMessage OpenAIStreamAccumulator::BuildAssistantMessage() const {
  LlmMessage m;
  m.role = LlmRole::kAssistant;
  m.content = content_;
  for (const auto& tc : tool_calls_) {
    if (tc.name.empty() && tc.id.empty() && tc.arguments_json.empty()) continue;
    LlmToolCall out;
    out.id = tc.id;
    out.name = tc.name;
    out.arguments_json = tc.arguments_json;
    m.tool_calls.push_back(std::move(out));
  }
  return m;
}

} // namespace agent
