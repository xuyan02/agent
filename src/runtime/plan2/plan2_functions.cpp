#include "runtime/plan2/plan2_functions.h"

#include <sstream>

#include "runtime/plan2/plan2_model.h"

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
      // non-string element not supported
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

static std::string task_to_json(const Task& t) {
  std::ostringstream oss;
  oss << '{';
  oss << "\"id\":\"" << json_escape(t.id) << "\"";
  oss << ",\"name\":\"" << json_escape(t.name) << "\"";
  oss << ",\"description\":\"" << json_escape(t.description) << "\"";
  oss << ",\"goal\":\"" << json_escape(t.goal) << "\"";
  oss << ",\"status\":\"" << json_escape(StatusToString(t.status)) << "\"";

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

  if (t.status == Status::kWaiting || t.status == Status::kAbandoned) {
    oss << ",\"reason\":\"" << json_escape(t.reason) << "\"";
  }

  oss << '}';
  return oss.str();
}

static std::string ok_empty() { return "{\"ok\":true}"; }
static std::string ok_deleted(const std::vector<std::string>& deleted) {
  std::ostringstream oss;
  oss << "{\"ok\":true,\"deleted\":" << ids_to_json_array(deleted) << '}';
  return oss.str();
}

static std::string ok_updated(const std::vector<Task>& updated) {
  std::ostringstream oss;
  oss << "{\"ok\":true,\"updated\":[";
  for (size_t i = 0; i < updated.size(); ++i) {
    if (i) oss << ',';
    oss << task_to_json(updated[i]);
  }
  oss << "]}";
  return oss.str();
}

static std::string ok_created(const Task& created) {
  std::ostringstream oss;
  oss << "{\"ok\":true,\"created\":" << task_to_json(created) << '}';
  return oss.str();
}

} // namespace

PlanAddTaskFunction::PlanAddTaskFunction(PlanModel* plan) : plan_(plan) {
  spec_.name = "plan.add_task";
  spec_.description = "Add one top-level task.";
  spec_.parameters_json =
      "{"
      "\"type\":\"object\","
      "\"properties\":{"
      "\"name\":{\"type\":\"string\"},"
      "\"description\":{\"type\":\"string\"},"
      "\"goal\":{\"type\":\"string\"},"
      "\"report_to\":{\"type\":\"string\"},"
      "\"depends_on\":{\"type\":\"array\",\"items\":{\"type\":\"string\"}}"
      "},"
      "\"required\":[\"name\",\"description\",\"goal\",\"report_to\"]"
      "}";
}

const agent::FunctionSpec& PlanAddTaskFunction::spec() const { return spec_; }

void PlanAddTaskFunction::InvokeAsync(std::string arguments_json, OnDone done) {
  AddTaskInput in;
  in.name = extract_json_string_or_empty(arguments_json, "name");
  in.description = extract_json_string_or_empty(arguments_json, "description");
  in.goal = extract_json_string_or_empty(arguments_json, "goal");

  const std::string report_to = extract_json_string_or_empty(arguments_json, "report_to");
  if (!report_to.empty()) in.report_to = report_to;

  in.depends_on = extract_json_string_array_or_empty(arguments_json, "depends_on");

  const AddTaskResult r = plan_->AddTask(in);
  if (!r.error.empty()) {
    std::move(done)("", r.error);
    return;
  }

  std::move(done)(ok_created(r.created), "");
}

PlanAddSubtaskFunction::PlanAddSubtaskFunction(PlanModel* plan) : plan_(plan) {
  spec_.name = "plan.add_subtask";
  spec_.description = "Add one subtask under a parent.";
  spec_.parameters_json =
      "{"
      "\"type\":\"object\","
      "\"properties\":{"
      "\"parent_id\":{\"type\":\"string\"},"
      "\"name\":{\"type\":\"string\"},"
      "\"description\":{\"type\":\"string\"},"
      "\"goal\":{\"type\":\"string\"},"
      "\"depends_on\":{\"type\":\"array\",\"items\":{\"type\":\"string\"}}"
      "},"
      "\"required\":[\"parent_id\",\"name\",\"description\",\"goal\"]"
      "}";
}

const agent::FunctionSpec& PlanAddSubtaskFunction::spec() const { return spec_; }

void PlanAddSubtaskFunction::InvokeAsync(std::string arguments_json, OnDone done) {
  AddSubtaskInput in;
  in.parent_id = extract_json_string_or_empty(arguments_json, "parent_id");
  in.name = extract_json_string_or_empty(arguments_json, "name");
  in.description = extract_json_string_or_empty(arguments_json, "description");
  in.goal = extract_json_string_or_empty(arguments_json, "goal");
  in.depends_on = extract_json_string_array_or_empty(arguments_json, "depends_on");

  const AddSubtaskResult r = plan_->AddSubtask(in);
  if (!r.error.empty()) {
    std::move(done)("", r.error);
    return;
  }

  std::move(done)(ok_created(r.created), "");
}

PlanActivateTaskFunction::PlanActivateTaskFunction(PlanModel* plan) : plan_(plan) {
  spec_.name = "plan.activate_task";
  spec_.description = "Activate a task (subtask activation auto-activates its ancestor chain).";
  spec_.parameters_json =
      "{"
      "\"type\":\"object\","
      "\"properties\":{"
      "\"id\":{\"type\":\"string\"}"
      "},"
      "\"required\":[\"id\"]"
      "}";
}

const agent::FunctionSpec& PlanActivateTaskFunction::spec() const { return spec_; }

void PlanActivateTaskFunction::InvokeAsync(std::string arguments_json, OnDone done) {
  const std::string id = extract_json_string_or_empty(arguments_json, "id");
  if (id.empty()) {
    std::move(done)("", "Missing argument: id");
    return;
  }

  const ActivateResult r = plan_->ActivateTask(id);
  if (!r.error.empty()) {
    std::move(done)("", r.error);
    return;
  }

  std::move(done)(ok_updated(r.updated), "");
}

PlanSuspendTaskFunction::PlanSuspendTaskFunction(PlanModel* plan) : plan_(plan) {
  spec_.name = "plan.suspend_task";
  spec_.description = "Suspend an active task into waiting with a required reason.";
  spec_.parameters_json =
      "{"
      "\"type\":\"object\","
      "\"properties\":{"
      "\"id\":{\"type\":\"string\"},"
      "\"reason\":{\"type\":\"string\"}"
      "},"
      "\"required\":[\"id\",\"reason\"]"
      "}";
}

const agent::FunctionSpec& PlanSuspendTaskFunction::spec() const { return spec_; }

void PlanSuspendTaskFunction::InvokeAsync(std::string arguments_json, OnDone done) {
  const std::string id = extract_json_string_or_empty(arguments_json, "id");
  const std::string reason = extract_json_string_or_empty(arguments_json, "reason");
  if (id.empty()) {
    std::move(done)("", "Missing argument: id");
    return;
  }

  const SimpleResult r = plan_->SuspendTask(id, reason);
  if (!r.error.empty()) {
    std::move(done)("", r.error);
    return;
  }

  std::move(done)(ok_updated(r.updated), "");
}

PlanResumeTaskFunction::PlanResumeTaskFunction(PlanModel* plan) : plan_(plan) {
  spec_.name = "plan.resume_task";
  spec_.description = "Resume a waiting task back to ready.";
  spec_.parameters_json =
      "{"
      "\"type\":\"object\","
      "\"properties\":{"
      "\"id\":{\"type\":\"string\"}"
      "},"
      "\"required\":[\"id\"]"
      "}";
}

const agent::FunctionSpec& PlanResumeTaskFunction::spec() const { return spec_; }

void PlanResumeTaskFunction::InvokeAsync(std::string arguments_json, OnDone done) {
  const std::string id = extract_json_string_or_empty(arguments_json, "id");
  if (id.empty()) {
    std::move(done)("", "Missing argument: id");
    return;
  }

  const SimpleResult r = plan_->ResumeTask(id);
  if (!r.error.empty()) {
    std::move(done)("", r.error);
    return;
  }

  std::move(done)(ok_updated(r.updated), "");
}

PlanCompleteTaskFunction::PlanCompleteTaskFunction(PlanModel* plan) : plan_(plan) {
  spec_.name = "plan.complete_task";
  spec_.description = "Complete an active task.";
  spec_.parameters_json =
      "{"
      "\"type\":\"object\","
      "\"properties\":{"
      "\"id\":{\"type\":\"string\"}"
      "},"
      "\"required\":[\"id\"]"
      "}";
}

const agent::FunctionSpec& PlanCompleteTaskFunction::spec() const { return spec_; }

void PlanCompleteTaskFunction::InvokeAsync(std::string arguments_json, OnDone done) {
  const std::string id = extract_json_string_or_empty(arguments_json, "id");
  if (id.empty()) {
    std::move(done)("", "Missing argument: id");
    return;
  }

  const SimpleResult r = plan_->CompleteTask(id);
  if (!r.error.empty()) {
    std::move(done)("", r.error);
    return;
  }

  std::move(done)(ok_updated(r.updated), "");
}

PlanAbandonTaskFunction::PlanAbandonTaskFunction(PlanModel* plan) : plan_(plan) {
  spec_.name = "plan.abandon_task";
  spec_.description = "Abandon a task with a required reason.";
  spec_.parameters_json =
      "{"
      "\"type\":\"object\","
      "\"properties\":{"
      "\"id\":{\"type\":\"string\"},"
      "\"reason\":{\"type\":\"string\"}"
      "},"
      "\"required\":[\"id\",\"reason\"]"
      "}";
}

const agent::FunctionSpec& PlanAbandonTaskFunction::spec() const { return spec_; }

void PlanAbandonTaskFunction::InvokeAsync(std::string arguments_json, OnDone done) {
  const std::string id = extract_json_string_or_empty(arguments_json, "id");
  const std::string reason = extract_json_string_or_empty(arguments_json, "reason");
  if (id.empty()) {
    std::move(done)("", "Missing argument: id");
    return;
  }

  const SimpleResult r = plan_->AbandonTask(id, reason);
  if (!r.error.empty()) {
    std::move(done)("", r.error);
    return;
  }

  std::move(done)(ok_updated(r.updated), "");
}

PlanRemoveTaskFunction::PlanRemoveTaskFunction(PlanModel* plan) : plan_(plan) {
  spec_.name = "plan.remove_task";
  spec_.description = "Remove a task and its subtree; also removes downstream dependent subtrees.";
  spec_.parameters_json =
      "{"
      "\"type\":\"object\","
      "\"properties\":{"
      "\"id\":{\"type\":\"string\"}"
      "},"
      "\"required\":[\"id\"]"
      "}";
}

const agent::FunctionSpec& PlanRemoveTaskFunction::spec() const { return spec_; }

void PlanRemoveTaskFunction::InvokeAsync(std::string arguments_json, OnDone done) {
  const std::string id = extract_json_string_or_empty(arguments_json, "id");
  if (id.empty()) {
    std::move(done)("", "Missing argument: id");
    return;
  }

  const RemoveTaskResult r = plan_->RemoveTask(id);
  if (!r.error.empty()) {
    std::move(done)("", r.error);
    return;
  }

  std::move(done)(ok_deleted(r.deleted), "");
}

PlanClearSubtasksFunction::PlanClearSubtasksFunction(PlanModel* plan) : plan_(plan) {
  spec_.name = "plan.clear_subtasks";
  spec_.description = "Clear all subtasks under a parent (deletes entire subtask subtree).";
  spec_.parameters_json =
      "{"
      "\"type\":\"object\","
      "\"properties\":{"
      "\"parent_id\":{\"type\":\"string\"}"
      "},"
      "\"required\":[\"parent_id\"]"
      "}";
}

const agent::FunctionSpec& PlanClearSubtasksFunction::spec() const { return spec_; }

void PlanClearSubtasksFunction::InvokeAsync(std::string arguments_json, OnDone done) {
  const std::string parent_id = extract_json_string_or_empty(arguments_json, "parent_id");
  if (parent_id.empty()) {
    std::move(done)("", "Missing argument: parent_id");
    return;
  }

  const ClearSubtasksResult r = plan_->ClearSubtasks(parent_id);
  if (!r.error.empty()) {
    std::move(done)("", r.error);
    return;
  }

  std::move(done)(ok_deleted(r.deleted), "");
}

} // namespace agent::plan2
