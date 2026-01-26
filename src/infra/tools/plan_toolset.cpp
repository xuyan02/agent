#include "infra/tools/plan_toolset.h"

#include <cctype>
#include <optional>
#include <string>
#include <vector>

namespace {

static std::string extract_json_string_or_empty(const std::string& json, const std::string& key) {
  auto pos = json.find('"' + key + '"');
  if (pos == std::string::npos) return "";
  pos = json.find(':', pos);
  if (pos == std::string::npos) return "";
  pos++;
  while (pos < json.size() && (json[pos] == ' ' || json[pos] == '\n' || json[pos] == '\r' || json[pos] == '\t')) pos++;
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

static size_t skip_ws(const std::string& s, size_t i) {
  while (i < s.size() && (s[i] == ' ' || s[i] == '\n' || s[i] == '\r' || s[i] == '\t')) i++;
  return i;
}

static size_t skip_string(const std::string& s, size_t i) {
  if (i >= s.size() || s[i] != '"') return i;
  i++;
  while (i < s.size()) {
    if (s[i] == '\\') {
      i += 2;
      continue;
    }
    if (s[i] == '"') return i + 1;
    i++;
  }
  return i;
}

static size_t skip_value(const std::string& s, size_t i);

static size_t skip_array(const std::string& s, size_t i) {
  if (i >= s.size() || s[i] != '[') return i;
  i++;
  for (;;) {
    i = skip_ws(s, i);
    if (i >= s.size()) return i;
    if (s[i] == ']') return i + 1;
    i = skip_value(s, i);
    i = skip_ws(s, i);
    if (i < s.size() && s[i] == ',') i++;
  }
}

static size_t skip_object(const std::string& s, size_t i) {
  if (i >= s.size() || s[i] != '{') return i;
  i++;
  for (;;) {
    i = skip_ws(s, i);
    if (i >= s.size()) return i;
    if (s[i] == '}') return i + 1;
    i = skip_string(s, i);
    i = skip_ws(s, i);
    if (i < s.size() && s[i] == ':') i++;
    i = skip_ws(s, i);
    i = skip_value(s, i);
    i = skip_ws(s, i);
    if (i < s.size() && s[i] == ',') i++;
  }
}

static size_t skip_value(const std::string& s, size_t i) {
  i = skip_ws(s, i);
  if (i >= s.size()) return i;
  if (s[i] == '"') return skip_string(s, i);
  if (s[i] == '{') return skip_object(s, i);
  if (s[i] == '[') return skip_array(s, i);
  while (i < s.size() && s[i] != ',' && s[i] != ']' && s[i] != '}') i++;
  return i;
}

static std::string extract_raw_field_or_empty(const std::string& json, const std::string& key) {
  auto pos = json.find('"' + key + '"');
  if (pos == std::string::npos) return {};
  pos = json.find(':', pos);
  if (pos == std::string::npos) return {};
  pos++;
  pos = skip_ws(json, pos);
  if (pos >= json.size()) return {};
  size_t end = skip_value(json, pos);
  if (end <= pos || end > json.size()) return {};
  return json.substr(pos, end - pos);
}

static bool parse_task_object(const std::string& obj_json, agent::Task* out) {
  agent::Task t;
  t.goal = extract_json_string_or_empty(obj_json, "goal");
  t.title = extract_json_string_or_empty(obj_json, "title");
  if (t.goal.empty() || t.title.empty()) {
    return false;
  }

  auto children_raw = extract_raw_field_or_empty(obj_json, "children");
  if (!children_raw.empty() && children_raw.front() == '[') {
    size_t i = 0;
    i++; // [
    for (;;) {
      i = skip_ws(children_raw, i);
      if (i >= children_raw.size() || children_raw[i] == ']') break;
      if (children_raw[i] != '{') {
        i++;
        continue;
      }
      size_t obj_end = skip_object(children_raw, i);
      if (obj_end <= i || obj_end > children_raw.size()) break;
      std::string child_obj = children_raw.substr(i, obj_end - i);
      agent::Task child;
      if (!parse_task_object(child_obj, &child)) return false;
      t.children.push_back(std::move(child));
      i = obj_end;
      i = skip_ws(children_raw, i);
      if (i < children_raw.size() && children_raw[i] == ',') i++;
    }
  }

  *out = std::move(t);
  return true;
}

static bool parse_new_children(const std::string& arguments_json, std::vector<agent::Task>* out) {
  auto raw = extract_raw_field_or_empty(arguments_json, "new_children");
  if (raw.empty()) {
    return false;
  }
  if (raw.front() != '[') {
    return false;
  }

  out->clear();
  size_t i = 0;
  i++; // [
  for (;;) {
    i = skip_ws(raw, i);
    if (i >= raw.size() || raw[i] == ']') break;
    if (raw[i] != '{') {
      i++;
      continue;
    }
    size_t obj_end = skip_object(raw, i);
    if (obj_end <= i || obj_end > raw.size()) break;
    std::string obj = raw.substr(i, obj_end - i);
    agent::Task t;
    if (!parse_task_object(obj, &t)) return false;
    out->push_back(std::move(t));
    i = obj_end;
    i = skip_ws(raw, i);
    if (i < raw.size() && raw[i] == ',') i++;
  }

  return true;
}

} // namespace

namespace agent {

PlanRenderTool::PlanRenderTool(std::shared_ptr<agent::PlanStore> store) : store_(std::move(store)) {}

agent::ToolResult PlanRenderTool::Invoke(const std::string& tool_call_id,
                                                  const std::string& /*arguments_json*/,
                                                  const agent::ToolContext& /*ctx*/) {
  agent::ToolResult tr;
  tr.tool_call_id = tool_call_id;
  tr.ok = true;
  tr.content = store_->render_markdown();
  return tr;
}

PlanAddTool::PlanAddTool(std::shared_ptr<agent::PlanStore> store) : store_(std::move(store)) {}

agent::ToolResult PlanAddTool::Invoke(const std::string& tool_call_id,
                                               const std::string& arguments_json,
                                               const agent::ToolContext& /*ctx*/) {
  agent::ToolResult tr;
  tr.tool_call_id = tool_call_id;

  auto goal = extract_json_string_or_empty(arguments_json, "goal");
  auto title = extract_json_string_or_empty(arguments_json, "title");
  if (goal.empty()) {
    tr.ok = false;
    tr.content = "Missing argument: goal";
    return tr;
  }
  if (title.empty()) {
    tr.ok = false;
    tr.content = "Missing argument: title";
    return tr;
  }

  std::optional<agent::TaskNo> parent;

  std::optional<agent::TaskNo> after;
  auto after_no_str = extract_json_string_or_empty(arguments_json, "after_no");
  if (!after_no_str.empty()) {
    after = agent::parse_task_no(after_no_str);
    if (!after) {
      tr.ok = false;
      tr.content = "Invalid after_no";
      return tr;
    }
  }

  auto res = store_->add(parent, goal, title, after);
  tr.ok = (res == "ok");
  tr.content = res;
  return tr;
}

PlanSwitchTool::PlanSwitchTool(std::shared_ptr<agent::PlanStore> store) : store_(std::move(store)) {}

agent::ToolResult PlanSwitchTool::Invoke(const std::string& tool_call_id,
                                                  const std::string& arguments_json,
                                                  const agent::ToolContext& /*ctx*/) {
  agent::ToolResult tr;
  tr.tool_call_id = tool_call_id;

  auto no_str = extract_json_string_or_empty(arguments_json, "no");
  auto no = agent::parse_task_no(no_str);
  if (!no) {
    tr.ok = false;
    tr.content = "Invalid no";
    return tr;
  }

  auto res = store_->switch_to(*no);
  tr.ok = (res == "ok");
  tr.content = res;
  return tr;
}

PlanCompleteTool::PlanCompleteTool(std::shared_ptr<agent::PlanStore> store) : store_(std::move(store)) {}

agent::ToolResult PlanCompleteTool::Invoke(const std::string& tool_call_id,
                                                    const std::string& arguments_json,
                                                    const agent::ToolContext& /*ctx*/) {
  agent::ToolResult tr;
  tr.tool_call_id = tool_call_id;

  auto no_str = extract_json_string_or_empty(arguments_json, "no");
  auto no = agent::parse_task_no(no_str);
  if (!no) {
    tr.ok = false;
    tr.content = "Invalid no";
    return tr;
  }

  auto res = store_->complete(*no);
  tr.ok = (res == "ok");
  tr.content = res;
  return tr;
}

PlanReplanTool::PlanReplanTool(std::shared_ptr<agent::PlanStore> store) : store_(std::move(store)) {}

agent::ToolResult PlanReplanTool::Invoke(const std::string& tool_call_id,
                                                  const std::string& arguments_json,
                                                  const agent::ToolContext& /*ctx*/) {
  agent::ToolResult tr;
  tr.tool_call_id = tool_call_id;

  auto no_str = extract_json_string_or_empty(arguments_json, "no");
  auto no = agent::parse_task_no(no_str);
  if (!no) {
    tr.ok = false;
    tr.content = "Invalid no";
    return tr;
  }

  auto history_line = extract_json_string_or_empty(arguments_json, "history_line");
  if (history_line.empty()) {
    tr.ok = false;
    tr.content = "Missing argument: history_line";
    return tr;
  }

  std::vector<agent::Task> new_children;
  if (!parse_new_children(arguments_json, &new_children)) {
    tr.ok = false;
    tr.content = "Invalid new_children";
    return tr;
  }

  auto res = store_->replan(*no, std::move(new_children), history_line);
  tr.ok = (res == "ok");
  tr.content = res;
  return tr;
}

} // namespace agent
