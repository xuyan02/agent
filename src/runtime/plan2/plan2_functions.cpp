#include "runtime/plan2/plan2_functions.h"

#include <sstream>

namespace agent::plan2 {
namespace {

static std::string json_escape(const std::string& s) {
  std::string out;
  out.reserve(s.size() + 8);
  for (char c : s) {
    switch (c) {
      case '\\':
        out += "\\\\";
        break;
      case '"':
        out += "\\\"";
        break;
      case '\n':
        out += "\\n";
        break;
      case '\r':
        out += "\\r";
        break;
      case '\t':
        out += "\\t";
        break;
      default:
        out.push_back(c);
        break;
    }
  }
  return out;
}

static std::string extract_json_string_or_empty(const std::string& json, const std::string& key) {
  auto pos = json.find('"' + key + '"');
  if (pos == std::string::npos) return {};
  pos = json.find(':', pos);
  if (pos == std::string::npos) return {};
  pos++;
  while (pos < json.size() && (json[pos] == ' ' || json[pos] == '\n' || json[pos] == '\r' || json[pos] == '\t')) pos++;
  if (pos >= json.size() || json[pos] != '"') return {};
  pos++;
  std::string out;
  for (; pos < json.size(); ++pos) {
    const char c = json[pos];
    if (c == '\\') {
      if (pos + 1 >= json.size()) break;
      out.push_back(json[pos + 1]);
      pos++;
      continue;
    }
    if (c == '"') break;
    out.push_back(c);
  }
  return out;
}

static std::string extract_json_object_for_key_or_empty(const std::string& json, const std::string& key) {
  auto pos = json.find('"' + key + '"');
  if (pos == std::string::npos) return {};
  pos = json.find(':', pos);
  if (pos == std::string::npos) return {};
  pos++;
  while (pos < json.size() && (json[pos] == ' ' || json[pos] == '\n' || json[pos] == '\r' || json[pos] == '\t')) pos++;
  if (pos >= json.size() || json[pos] != '{') return {};

  int depth = 0;
  size_t start = pos;
  for (; pos < json.size(); ++pos) {
    const char c = json[pos];
    if (c == '"') {
      // skip string
      pos++;
      while (pos < json.size()) {
        if (json[pos] == '\\') {
          pos += 2;
          continue;
        }
        if (json[pos] == '"') break;
        pos++;
      }
      continue;
    }
    if (c == '{') depth++;
    if (c == '}') {
      depth--;
      if (depth == 0) {
        const size_t end = pos + 1;
        return json.substr(start, end - start);
      }
    }
  }
  return {};
}

static std::vector<std::string> extract_json_string_array_or_empty(const std::string& json, const std::string& key) {
  std::vector<std::string> out;
  auto pos = json.find('"' + key + '"');
  if (pos == std::string::npos) return out;
  pos = json.find(':', pos);
  if (pos == std::string::npos) return out;
  pos++;
  while (pos < json.size() && (json[pos] == ' ' || json[pos] == '\n' || json[pos] == '\r' || json[pos] == '\t')) pos++;
  if (pos >= json.size() || json[pos] != '[') return out;
  pos++;

  while (pos < json.size()) {
    while (pos < json.size() && (json[pos] == ' ' || json[pos] == '\n' || json[pos] == '\r' || json[pos] == '\t')) pos++;
    if (pos >= json.size()) break;
    if (json[pos] == ']') break;
    if (json[pos] == ',') {
      pos++;
      continue;
    }
    if (json[pos] != '"') {
      // non-string element not supported in MVP
      return {};
    }
    pos++;
    std::string s;
    for (; pos < json.size(); ++pos) {
      const char c = json[pos];
      if (c == '\\') {
        if (pos + 1 >= json.size()) break;
        s.push_back(json[pos + 1]);
        pos++;
        continue;
      }
      if (c == '"') break;
      s.push_back(c);
    }
    out.push_back(std::move(s));
    if (pos < json.size() && json[pos] == '"') pos++;
  }

  return out;
}

static std::vector<std::string> extract_tasks_array_objects(const std::string& arguments_json) {
  // Extract tasks: [ {..}, {..} ] as raw object strings.
  std::vector<std::string> out;

  auto pos = arguments_json.find("\"tasks\"");
  if (pos == std::string::npos) return out;
  pos = arguments_json.find('[', pos);
  if (pos == std::string::npos) return out;
  pos++;

  while (pos < arguments_json.size()) {
    while (pos < arguments_json.size() &&
           (arguments_json[pos] == ' ' || arguments_json[pos] == '\n' || arguments_json[pos] == '\r' ||
            arguments_json[pos] == '\t' || arguments_json[pos] == ',')) {
      pos++;
    }
    if (pos >= arguments_json.size() || arguments_json[pos] == ']') break;
    if (arguments_json[pos] != '{') return {};

    int depth = 0;
    const size_t start = pos;
    for (; pos < arguments_json.size(); ++pos) {
      const char c = arguments_json[pos];
      if (c == '"') {
        pos++;
        while (pos < arguments_json.size()) {
          if (arguments_json[pos] == '\\') {
            pos += 2;
            continue;
          }
          if (arguments_json[pos] == '"') break;
          pos++;
        }
        continue;
      }
      if (c == '{') depth++;
      if (c == '}') {
        depth--;
        if (depth == 0) {
          const size_t end = pos + 1;
          out.push_back(arguments_json.substr(start, end - start));
          pos = end;
          break;
        }
      }
    }
  }

  return out;
}

static std::string status_to_json(Status s) {
  switch (s) {
    case Status::kPending:
      return "\"pending\"";
    case Status::kNotReady:
      return "\"not_ready\"";
    case Status::kBlocked:
      return "\"blocked\"";
    case Status::kInProgress:
      return "\"in_progress\"";
    case Status::kDone:
      return "\"done\"";
    case Status::kCanceled:
      return "\"canceled\"";
    case Status::kFailed:
      return "\"failed\"";
  }
  return "\"unknown\"";
}

static bool parse_status(const std::string& s, Status* out) {
  if (s == "pending") {
    *out = Status::kPending;
    return true;
  }
  if (s == "not_ready") {
    *out = Status::kNotReady;
    return true;
  }
  if (s == "blocked") {
    *out = Status::kBlocked;
    return true;
  }
  if (s == "in_progress") {
    *out = Status::kInProgress;
    return true;
  }
  if (s == "done") {
    *out = Status::kDone;
    return true;
  }
  if (s == "canceled") {
    *out = Status::kCanceled;
    return true;
  }
  if (s == "failed") {
    *out = Status::kFailed;
    return true;
  }
  return false;
}

static std::string task_to_json(const Task& t) {
  std::ostringstream oss;
  oss << '{';
  oss << "\"id\":\"" << json_escape(t.id) << "\"";
  oss << ",\"title\":\"" << json_escape(t.title) << "\"";
  oss << ",\"status\":" << status_to_json(t.status);

  if (t.parent_id.has_value()) {
    oss << ",\"parent_id\":\"" << json_escape(*t.parent_id) << "\"";
  }

  oss << ",\"depends_on\":[";
  for (size_t i = 0; i < t.depends_on.size(); ++i) {
    if (i) oss << ',';
    oss << "\"" << json_escape(t.depends_on[i]) << "\"";
  }
  oss << ']';

  if (!t.parent_id.has_value() && t.report_to.has_value()) {
    oss << ",\"report_to\":\"" << json_escape(*t.report_to) << "\"";
  }

  if (t.status == Status::kBlocked) {
    oss << ",\"block_reason\":\"" << json_escape(t.block_reason) << "\"";
  }

  if (!t.detail.empty()) {
    oss << ",\"detail\":\"" << json_escape(t.detail) << "\"";
  }

  oss << '}';
  return oss.str();
}

static std::string ids_to_json_array(const std::vector<std::string>& ids) {
  std::ostringstream oss;
  oss << '[';
  for (size_t i = 0; i < ids.size(); ++i) {
    if (i) oss << ',';
    oss << "\"" << json_escape(ids[i]) << "\"";
  }
  oss << ']';
  return oss.str();
}

} // namespace

PlanAddTasksFunction::PlanAddTasksFunction(PlanModel* plan) : plan_(plan) {
  spec_.name = "plan.add_tasks";
  spec_.description = "Add one or more tasks to the plan.";
  spec_.parameters_json =
      "{"
      "\"type\":\"object\","
      "\"properties\":{"
      "\"tasks\":{"
      "\"type\":\"array\","
      "\"items\":{"
      "\"type\":\"object\","
      "\"properties\":{"
      "\"title\":{\"type\":\"string\"},"
      "\"detail\":{\"type\":\"string\"},"
      "\"parent_id\":{\"type\":\"string\"},"
      "\"depends_on\":{\"type\":\"array\",\"items\":{\"type\":\"string\"}},"
      "\"report_to\":{\"type\":\"string\"}"
      "},"
      "\"required\":[\"title\"]"
      "}"
      "}"
      "},"
      "\"required\":[\"tasks\"]"
      "}";
}

const agent::FunctionSpec& PlanAddTasksFunction::spec() const { return spec_; }

bool PlanAddTasksFunction::Invoke(std::string arguments_json, std::string* out_result_json, std::string* out_error) {
  const auto tasks = extract_tasks_array_objects(arguments_json);
  if (tasks.empty()) {
    *out_error = "Missing argument: tasks";
    return false;
  }

  std::vector<AddTaskInput> inputs;
  inputs.reserve(tasks.size());
  for (const auto& t : tasks) {
    AddTaskInput in;
    in.title = extract_json_string_or_empty(t, "title");
    in.detail = extract_json_string_or_empty(t, "detail");

    const std::string parent = extract_json_string_or_empty(t, "parent_id");
    if (!parent.empty()) in.parent_id = parent;

    in.depends_on = extract_json_string_array_or_empty(t, "depends_on");

    const std::string report_to = extract_json_string_or_empty(t, "report_to");
    if (!report_to.empty()) in.report_to = report_to;

    inputs.push_back(std::move(in));
  }

  const AddTasksResult r = plan_->AddTasks(inputs);
  if (!r.error.empty()) {
    *out_error = r.error;
    return false;
  }

  std::ostringstream oss;
  oss << '{';
  oss << "\"created\":[";
  for (size_t i = 0; i < r.created.size(); ++i) {
    if (i) oss << ',';
    oss << task_to_json(r.created[i]);
  }
  oss << ']';

  oss << ",\"warnings\":[";
  for (size_t i = 0; i < r.warnings.size(); ++i) {
    if (i) oss << ',';
    oss << "\"" << json_escape(r.warnings[i]) << "\"";
  }
  oss << ']';

  oss << '}';

  *out_result_json = oss.str();
  return true;
}

PlanSetStatusFunction::PlanSetStatusFunction(PlanModel* plan) : plan_(plan) {
  spec_.name = "plan.set_status";
  spec_.description = "Set task status with dependency rules and cascading effects.";
  spec_.parameters_json =
      "{"
      "\"type\":\"object\","
      "\"properties\":{"
      "\"task_id\":{\"type\":\"string\"},"
      "\"status\":{\"type\":\"string\",\"enum\":[\"pending\",\"not_ready\",\"blocked\",\"in_progress\",\"done\",\"canceled\",\"failed\"]},"
      "\"block_reason\":{\"type\":\"string\"}"
      "},"
      "\"required\":[\"task_id\",\"status\"]"
      "}";
}

const agent::FunctionSpec& PlanSetStatusFunction::spec() const { return spec_; }

bool PlanSetStatusFunction::Invoke(std::string arguments_json, std::string* out_result_json, std::string* out_error) {
  const std::string task_id = extract_json_string_or_empty(arguments_json, "task_id");
  if (task_id.empty()) {
    *out_error = "Missing argument: task_id";
    return false;
  }

  const std::string status_s = extract_json_string_or_empty(arguments_json, "status");
  Status st;
  if (!parse_status(status_s, &st)) {
    *out_error = "Invalid argument: status";
    return false;
  }

  const std::string block_reason = extract_json_string_or_empty(arguments_json, "block_reason");

  const SetStatusResult r = plan_->SetStatus(task_id, st, block_reason);
  if (!r.error.empty()) {
    *out_error = r.error;
    return false;
  }

  std::ostringstream oss;
  oss << '{';
  oss << "\"updated\":[";
  for (size_t i = 0; i < r.updated.size(); ++i) {
    if (i) oss << ',';
    oss << task_to_json(r.updated[i]);
  }
  oss << ']';

  oss << ",\"canceled_cascade\":[";
  for (size_t i = 0; i < r.canceled_cascade.size(); ++i) {
    if (i) oss << ',';
    oss << task_to_json(r.canceled_cascade[i]);
  }
  oss << ']';

  oss << ",\"deleted\":" << ids_to_json_array(r.deleted);

  oss << ",\"normalized\":[";
  for (size_t i = 0; i < r.normalized.size(); ++i) {
    if (i) oss << ',';
    oss << task_to_json(r.normalized[i]);
  }
  oss << ']';

  oss << ",\"warnings\":[";
  for (size_t i = 0; i < r.warnings.size(); ++i) {
    if (i) oss << ',';
    oss << "\"" << json_escape(r.warnings[i]) << "\"";
  }
  oss << ']';

  oss << '}';

  *out_result_json = oss.str();
  return true;
}

PlanRemoveTaskFunction::PlanRemoveTaskFunction(PlanModel* plan) : plan_(plan) {
  spec_.name = "plan.remove_task";
  spec_.description = "Remove a task and its subtree; also removes downstream dependent subtrees.";
  spec_.parameters_json =
      "{"
      "\"type\":\"object\","
      "\"properties\":{"
      "\"task_id\":{\"type\":\"string\"}"
      "},"
      "\"required\":[\"task_id\"]"
      "}";
}

const agent::FunctionSpec& PlanRemoveTaskFunction::spec() const { return spec_; }

bool PlanRemoveTaskFunction::Invoke(std::string arguments_json, std::string* out_result_json, std::string* out_error) {
  const std::string task_id = extract_json_string_or_empty(arguments_json, "task_id");
  if (task_id.empty()) {
    *out_error = "Missing argument: task_id";
    return false;
  }

  const RemoveTaskResult r = plan_->RemoveTask(task_id);
  if (!r.error.empty()) {
    *out_error = r.error;
    return false;
  }

  std::ostringstream oss;
  oss << '{';
  oss << "\"deleted\":" << ids_to_json_array(r.deleted);

  oss << ",\"warnings\":[";
  for (size_t i = 0; i < r.warnings.size(); ++i) {
    if (i) oss << ',';
    oss << "\"" << json_escape(r.warnings[i]) << "\"";
  }
  oss << ']';

  oss << '}';

  *out_result_json = oss.str();
  return true;
}

} // namespace agent::plan2
