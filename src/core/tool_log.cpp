#include "core/tool_log.h"

#include "core/conversation.h"

#include <algorithm>
#include <cctype>
#include <sstream>

namespace agent {

namespace {

static std::string trim(const std::string& s) {
  size_t b = 0;
  while (b < s.size() && std::isspace(static_cast<unsigned char>(s[b]))) b++;
  size_t e = s.size();
  while (e > b && std::isspace(static_cast<unsigned char>(s[e - 1]))) e--;
  return s.substr(b, e - b);
}

static std::string clip(const std::string& s, size_t max_len) {
  if (s.size() <= max_len) return s;
  return s.substr(0, max_len) + "…";
}

static std::string extract_json_string_or_empty(const std::string& json, const std::string& key) {
  auto pos = json.find('"' + key + '"');
  if (pos == std::string::npos) return "";
  pos = json.find(':', pos);
  if (pos == std::string::npos) return "";
  pos++;
  while (pos < json.size() && std::isspace(static_cast<unsigned char>(json[pos]))) pos++;
  if (pos >= json.size() || json[pos] != '"') return "";
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

static std::string summarize_args(const ToolCall& tc) {
  const auto& j = tc.arguments_json;

  if (tc.name == "plan") {
    auto method = extract_json_string_or_empty(j, "method");
    if (method.empty()) method = extract_json_string_or_empty(j, "op");
    std::ostringstream oss;
    oss << method;
    auto no = extract_json_string_or_empty(j, "no");
    if (!no.empty()) oss << " no=" << no;
    auto parent_no = extract_json_string_or_empty(j, "parent_no");
    if (!parent_no.empty()) oss << " parent_no=" << parent_no;
    auto after_no = extract_json_string_or_empty(j, "after_no");
    if (!after_no.empty()) oss << " after_no=" << after_no;
    auto title = extract_json_string_or_empty(j, "title");
    if (!title.empty()) oss << " title=\"" << clip(title, 80) << "\"";
    auto history = extract_json_string_or_empty(j, "history_line");
    if (!history.empty()) oss << " history=\"" << clip(history, 80) << "\"";
    return trim(oss.str());
  }

  if (tc.name == "read_file") {
    auto path = extract_json_string_or_empty(j, "path");
    return path.empty() ? "" : ("path=" + path);
  }

  if (tc.name == "write_file") {
    auto path = extract_json_string_or_empty(j, "path");
    auto content = extract_json_string_or_empty(j, "content");
    std::ostringstream oss;
    if (!path.empty()) oss << "path=" << path;
    if (!content.empty()) oss << " bytes=" << content.size();
    return trim(oss.str());
  }

  if (tc.name == "run_shell_command") {
    auto cmd = extract_json_string_or_empty(j, "command");
    return cmd.empty() ? "" : ("command=\"" + clip(cmd, 120) + "\"");
  }

  if (tc.name == "echo") {
    auto msg = extract_json_string_or_empty(j, "message");
    return msg.empty() ? "" : ("message=\"" + clip(msg, 120) + "\"");
  }

  // Fallback: show a clipped raw args.
  return clip(trim(j), 160);
}

static std::string summarize_result(const ToolResult& tr) {
  if (!tr.ok) {
    return tr.content.empty() ? "error" : ("error: " + clip(trim(tr.content), 160));
  }
  if (tr.content.empty()) return "ok";
  return clip(trim(tr.content), 160);
}

} // namespace

std::string format_tool_log_line(const ToolCall& tc, const ToolResult& tr) {
  std::ostringstream oss;
  oss << "[tool] " << tc.name;
  auto args = summarize_args(tc);
  if (!args.empty()) oss << " (" << args << ")";
  oss << " -> " << (tr.ok ? "ok" : "fail");
  auto res = summarize_result(tr);
  if (!res.empty() && res != "ok") oss << " | " << res;
  return oss.str();
}

} // namespace agent
