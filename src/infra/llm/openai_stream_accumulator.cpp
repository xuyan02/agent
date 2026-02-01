#include "infra/llm/openai_stream_accumulator.h"

#include "infra/json/json.h"

#include <nlohmann/json.hpp>

#include <cstddef>
#include <cstdlib>
#include <iostream>
#include <optional>

namespace agent {

void OpenAIStreamAccumulator::Reset() {
  content_.clear();
  tool_calls_.clear();
}

void OpenAIStreamAccumulator::EnsureToolIndex(size_t idx) {
  while (tool_calls_.size() <= idx) tool_calls_.push_back({});
}

static std::optional<nlohmann::json> extract_delta_obj(const std::string& data_line) {
  // We parse each SSE data line as a JSON object and then read choices[0].delta.
  // This is more robust than substring scanning and removes the json_min dependency.
  auto root_opt = agent::json::Parse(data_line);
  if (!root_opt) return std::nullopt;
  const auto& root = *root_opt;

  if (!root.is_object()) return std::nullopt;
  auto cit = root.find("choices");
  if (cit == root.end() || !cit->is_array() || cit->empty()) return std::nullopt;
  const auto& choice0 = (*cit)[0];
  if (!choice0.is_object()) return std::nullopt;
  auto dit = choice0.find("delta");
  if (dit == choice0.end() || !dit->is_object()) return std::nullopt;
  return *dit;
}

static std::optional<std::string> extract_finish_reason(const std::string& data_line) {
  auto root_opt = agent::json::Parse(data_line);
  if (!root_opt) return std::nullopt;
  const auto& root = *root_opt;

  if (!root.is_object()) return std::nullopt;
  auto cit = root.find("choices");
  if (cit == root.end() || !cit->is_array() || cit->empty()) return std::nullopt;
  const auto& choice0 = (*cit)[0];
  if (!choice0.is_object()) return std::nullopt;

  auto fit = choice0.find("finish_reason");
  if (fit == choice0.end() || fit->is_null()) return std::string{};
  if (!fit->is_string()) return std::nullopt;
  return fit->get<std::string>();
}

bool OpenAIStreamAccumulator::FeedDataLine(const std::string& data_line, OpenAIStreamDelta* out_delta) {
  if (out_delta) *out_delta = {};
  if (data_line == "[DONE]") {
    if (out_delta) out_delta->has_finish_reason = true;
    return true;
  }

  const bool dbg = std::getenv("CPP_AGENT_DEBUG_LLM_VERBOSE") != nullptr;

  // Parse delta object from the SSE JSON line.
  auto delta_opt = extract_delta_obj(data_line);
  if (!delta_opt) return true; // best-effort, ignore non-json / unexpected lines
  const auto& delta = *delta_opt;

  // content delta
  auto cit = delta.find("content");
  if (cit != delta.end() && cit->is_string()) {
    const std::string tok = cit->get<std::string>();
    content_ += tok;
    if (out_delta) out_delta->content_delta = tok;
  }

  // finish_reason (if present and non-null it signals end of round)
  auto fr_opt = extract_finish_reason(data_line);
  if (fr_opt && !fr_opt->empty()) {
    if (out_delta) out_delta->has_finish_reason = true;
  }

  // tool_calls delta
  auto tci = delta.find("tool_calls");
  if (tci != delta.end() && tci->is_array()) {
    if (dbg) {
      std::string s = tci->dump();
      if (s.size() > 512) s = s.substr(0, 512) + "...(truncated)";
      std::cerr << "[cpp-agent.llm] tool_calls_json len=" << tci->dump().size() << " head=" << s << "\n";
    }

    for (const auto& obj : *tci) {
      if (!obj.is_object()) continue;

      size_t idx = 0;
      auto idx_it = obj.find("index");
      if (idx_it != obj.end() && idx_it->is_number_unsigned()) {
        idx = idx_it->get<size_t>();
      } else if (idx_it != obj.end() && idx_it->is_number_integer()) {
        const auto v = idx_it->get<long long>();
        if (v >= 0) idx = static_cast<size_t>(v);
      }
      EnsureToolIndex(idx);

      auto id_it = obj.find("id");
      if (id_it != obj.end() && id_it->is_string()) {
        tool_calls_[idx].id = id_it->get<std::string>();
        if (dbg) std::cerr << "[cpp-agent.llm] tc.delta idx=" << idx << " id=" << tool_calls_[idx].id << "\n";
      }

      auto fn_it = obj.find("function");
      if (fn_it != obj.end() && fn_it->is_object()) {
        auto name_it = fn_it->find("name");
        if (name_it != fn_it->end() && name_it->is_string()) {
          tool_calls_[idx].name = name_it->get<std::string>();
          if (dbg) std::cerr << "[cpp-agent.llm] tc.delta idx=" << idx << " name=" << tool_calls_[idx].name << "\n";
        }

        auto args_it = fn_it->find("arguments");
        if (args_it != fn_it->end() && args_it->is_string()) {
          const std::string args_chunk = args_it->get<std::string>();
          tool_calls_[idx].arguments_json += args_chunk;
          if (dbg) {
            std::cerr << "[cpp-agent.llm] tc.delta idx=" << idx << " args.delta.len=" << args_chunk.size()
                      << " args.total.len=" << tool_calls_[idx].arguments_json.size() << "\n";
          }
          if (out_delta) {
            LlmToolCall tc;
            tc.id = tool_calls_[idx].id;
            tc.name = tool_calls_[idx].name;
            tc.arguments_json = args_chunk;
            out_delta->tool_calls_delta.push_back(std::move(tc));
          }
        }
      }
    }
  }

  return true;
}

bool OpenAIStreamAccumulator::HasToolCalls() const {
  const bool dbg = std::getenv("CPP_AGENT_DEBUG_LLM_VERBOSE") != nullptr;
  if (dbg) {
    std::cerr << "[cpp-agent.llm] HasToolCalls tool_calls_.size=" << tool_calls_.size() << "\n";
    for (size_t i = 0; i < tool_calls_.size(); i++) {
      const auto& tc = tool_calls_[i];
      std::cerr << "[cpp-agent.llm]  acc.tc[" << i << "] id.len=" << tc.id.size()
                << " name.len=" << tc.name.size() << " args.len=" << tc.arguments_json.size() << "\n";
    }
  }

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
