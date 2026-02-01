#include "runtime/plan2/plan2_functions.h"

#include "infra/json/json.h"

#include <nlohmann/json.hpp>

#include <sstream>

#include "runtime/plan2/plan2_model.h"

namespace agent::plan2 {
namespace {

static std::optional<std::string> get_string_required(const nlohmann::json& obj, const char* key) {
  if (!obj.is_object())
    return std::nullopt;
  auto it = obj.find(key);
  if (it == obj.end() || !it->is_string())
    return std::nullopt;
  return it->get<std::string>();
}

static std::vector<std::string> get_string_array_or_empty(const nlohmann::json& obj, const char* key) {
  std::vector<std::string> out;
  if (!obj.is_object())
    return out;
  auto it = obj.find(key);
  if (it == obj.end() || !it->is_array())
    return out;
  for (const auto& v : *it) {
    if (v.is_string())
      out.push_back(v.get<std::string>());
  }
  return out;
}

static nlohmann::json ids_to_json_array(const std::vector<std::string>& ids) {
  nlohmann::json arr = nlohmann::json::array();
  for (const auto& id : ids)
    arr.push_back(id);
  return arr;
}

static nlohmann::json task_to_json_obj(const Task& t) {
  nlohmann::json obj = nlohmann::json::object();
  obj["id"] = t.id;
  obj["name"] = t.name;
  obj["description"] = t.description;
  obj["goal"] = t.goal;
  obj["status"] = StatusToString(t.status);

  if (t.parent_id.has_value())
    obj["parent_id"] = *t.parent_id;

  obj["depends_on"] = t.depends_on;

  if (!t.parent_id.has_value() && t.report_to.has_value())
    obj["report_to"] = *t.report_to;

  if (t.status == Status::kWaiting || t.status == Status::kAbandoned)
    obj["reason"] = t.reason;

  return obj;
}

static std::string ok_empty() {
  nlohmann::json root = nlohmann::json::object();
  root["ok"] = true;
  return agent::json::Dump(root);
}

static std::string ok_deleted(const std::vector<std::string>& deleted) {
  nlohmann::json root = nlohmann::json::object();
  root["ok"] = true;
  root["deleted"] = ids_to_json_array(deleted);
  return agent::json::Dump(root);
}

static std::string ok_updated(const std::vector<Task>& updated) {
  nlohmann::json root = nlohmann::json::object();
  root["ok"] = true;

  nlohmann::json arr = nlohmann::json::array();
  for (const auto& t : updated)
    arr.push_back(task_to_json_obj(t));
  root["updated"] = std::move(arr);

  return agent::json::Dump(root);
}

static std::string ok_created(const Task& created) {
  nlohmann::json root = nlohmann::json::object();
  root["ok"] = true;
  root["created"] = task_to_json_obj(created);
  return agent::json::Dump(root);
}

static std::string schema_add_task() {
  nlohmann::json root = nlohmann::json::object();
  root["type"] = "object";

  nlohmann::json props = nlohmann::json::object();
  props["name"] = nlohmann::json::object({{"type", "string"}});
  props["description"] = nlohmann::json::object({{"type", "string"}});
  props["goal"] = nlohmann::json::object({{"type", "string"}});
  props["report_to"] = nlohmann::json::object({{"type", "string"}});
  props["depends_on"] = nlohmann::json::object(
      {{"type", "array"}, {"items", nlohmann::json::object({{"type", "string"}})}});
  root["properties"] = std::move(props);

  root["required"] = nlohmann::json::array({"name", "description", "goal", "report_to"});
  return agent::json::Dump(root);
}

} // namespace

PlanAddTaskFunction::PlanAddTaskFunction(PlanModel* plan) : plan_(plan) {
  spec_.name = "plan.add_task";
  spec_.description = "Add one top-level task.";
  spec_.parameters_json = schema_add_task();
}

const agent::FunctionSpec& PlanAddTaskFunction::spec() const { return spec_; }

void PlanAddTaskFunction::InvokeAsync(std::string arguments_json, OnDone done) {
  auto args_opt = agent::json::Parse(arguments_json);
  if (!args_opt) {
    std::move(done)("", "invalid JSON arguments");
    return;
  }

  const auto& args = *args_opt;

  AddTaskInput in;
  auto name = get_string_required(args, "name");
  auto description = get_string_required(args, "description");
  auto goal = get_string_required(args, "goal");
  auto report_to = get_string_required(args, "report_to");
  if (!name || !description || !goal || !report_to) {
    std::move(done)("", "missing required string field");
    return;
  }

  in.name = *name;
  in.description = *description;
  in.goal = *goal;
  in.report_to = *report_to;
  in.depends_on = get_string_array_or_empty(args, "depends_on");

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

  nlohmann::json root = nlohmann::json::object();
  root["type"] = "object";

  nlohmann::json props = nlohmann::json::object();
  props["parent_id"] = nlohmann::json::object({{"type", "string"}});
  props["name"] = nlohmann::json::object({{"type", "string"}});
  props["description"] = nlohmann::json::object({{"type", "string"}});
  props["goal"] = nlohmann::json::object({{"type", "string"}});
  props["depends_on"] = nlohmann::json::object(
      {{"type", "array"}, {"items", nlohmann::json::object({{"type", "string"}})}});
  root["properties"] = std::move(props);

  root["required"] = nlohmann::json::array({"parent_id", "name", "description", "goal"});
  spec_.parameters_json = agent::json::Dump(root);
}

const agent::FunctionSpec& PlanAddSubtaskFunction::spec() const { return spec_; }

void PlanAddSubtaskFunction::InvokeAsync(std::string arguments_json, OnDone done) {
  auto args_opt = agent::json::Parse(arguments_json);
  if (!args_opt) {
    std::move(done)("", "invalid JSON arguments");
    return;
  }

  const auto& args = *args_opt;

  AddSubtaskInput in;
  auto parent_id = get_string_required(args, "parent_id");
  auto name = get_string_required(args, "name");
  auto description = get_string_required(args, "description");
  auto goal = get_string_required(args, "goal");
  if (!parent_id || !name || !description || !goal) {
    std::move(done)("", "missing required string field");
    return;
  }

  in.parent_id = *parent_id;
  in.name = *name;
  in.description = *description;
  in.goal = *goal;
  in.depends_on = get_string_array_or_empty(args, "depends_on");

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
  auto args_opt = agent::json::Parse(arguments_json);
  if (!args_opt) {
    std::move(done)("", "invalid JSON arguments");
    return;
  }

  auto id = get_string_required(*args_opt, "id");
  if (!id) {
    std::move(done)("", "Missing argument: id");
    return;
  }

  const ActivateResult r = plan_->ActivateTask(*id);
  if (!r.error.empty()) {
    std::move(done)("", r.error);
    return;
  }

  std::move(done)(ok_updated(r.updated), "");
}

PlanSuspendTaskFunction::PlanSuspendTaskFunction(PlanModel* plan) : plan_(plan) {
  spec_.name = "plan.suspend_task";
  spec_.description = "Suspend an active task into waiting with a required reason.";

  nlohmann::json root = nlohmann::json::object();
  root["type"] = "object";
  root["properties"] = nlohmann::json::object({
      {"id", nlohmann::json::object({{"type", "string"}})},
      {"reason", nlohmann::json::object({{"type", "string"}})},
  });
  root["required"] = nlohmann::json::array({"id", "reason"});
  spec_.parameters_json = agent::json::Dump(root);
}

const agent::FunctionSpec& PlanSuspendTaskFunction::spec() const { return spec_; }

void PlanSuspendTaskFunction::InvokeAsync(std::string arguments_json, OnDone done) {
  auto args_opt = agent::json::Parse(arguments_json);
  if (!args_opt) {
    std::move(done)("", "invalid JSON arguments");
    return;
  }

  auto id = get_string_required(*args_opt, "id");
  auto reason = get_string_required(*args_opt, "reason");
  if (!id) {
    std::move(done)("", "Missing argument: id");
    return;
  }
  if (!reason) {
    std::move(done)("", "Missing argument: reason");
    return;
  }

  const SimpleResult r = plan_->SuspendTask(*id, *reason);
  if (!r.error.empty()) {
    std::move(done)("", r.error);
    return;
  }

  std::move(done)(ok_updated(r.updated), "");
}

PlanResumeTaskFunction::PlanResumeTaskFunction(PlanModel* plan) : plan_(plan) {
  spec_.name = "plan.resume_task";
  spec_.description = "Resume a waiting task back to ready.";

  nlohmann::json root = nlohmann::json::object();
  root["type"] = "object";
  root["properties"] = nlohmann::json::object({
      {"id", nlohmann::json::object({{"type", "string"}})},
  });
  root["required"] = nlohmann::json::array({"id"});
  spec_.parameters_json = agent::json::Dump(root);
}

const agent::FunctionSpec& PlanResumeTaskFunction::spec() const { return spec_; }

void PlanResumeTaskFunction::InvokeAsync(std::string arguments_json, OnDone done) {
  auto args_opt = agent::json::Parse(arguments_json);
  if (!args_opt) {
    std::move(done)("", "invalid JSON arguments");
    return;
  }

  auto id = get_string_required(*args_opt, "id");
  if (!id) {
    std::move(done)("", "Missing argument: id");
    return;
  }

  const SimpleResult r = plan_->ResumeTask(*id);
  if (!r.error.empty()) {
    std::move(done)("", r.error);
    return;
  }

  std::move(done)(ok_updated(r.updated), "");
}

PlanCompleteTaskFunction::PlanCompleteTaskFunction(PlanModel* plan) : plan_(plan) {
  spec_.name = "plan.complete_task";
  spec_.description = "Complete an active task.";

  nlohmann::json root = nlohmann::json::object();
  root["type"] = "object";
  root["properties"] = nlohmann::json::object({
      {"id", nlohmann::json::object({{"type", "string"}})},
  });
  root["required"] = nlohmann::json::array({"id"});
  spec_.parameters_json = agent::json::Dump(root);
}

const agent::FunctionSpec& PlanCompleteTaskFunction::spec() const { return spec_; }

void PlanCompleteTaskFunction::InvokeAsync(std::string arguments_json, OnDone done) {
  auto args_opt = agent::json::Parse(arguments_json);
  if (!args_opt) {
    std::move(done)("", "invalid JSON arguments");
    return;
  }

  auto id = get_string_required(*args_opt, "id");
  if (!id) {
    std::move(done)("", "Missing argument: id");
    return;
  }

  const SimpleResult r = plan_->CompleteTask(*id);
  if (!r.error.empty()) {
    std::move(done)("", r.error);
    return;
  }

  std::move(done)(ok_updated(r.updated), "");
}

PlanAbandonTaskFunction::PlanAbandonTaskFunction(PlanModel* plan) : plan_(plan) {
  spec_.name = "plan.abandon_task";
  spec_.description = "Abandon a task with a required reason.";

  nlohmann::json root = nlohmann::json::object();
  root["type"] = "object";
  root["properties"] = nlohmann::json::object({
      {"id", nlohmann::json::object({{"type", "string"}})},
      {"reason", nlohmann::json::object({{"type", "string"}})},
  });
  root["required"] = nlohmann::json::array({"id", "reason"});
  spec_.parameters_json = agent::json::Dump(root);
}

const agent::FunctionSpec& PlanAbandonTaskFunction::spec() const { return spec_; }

void PlanAbandonTaskFunction::InvokeAsync(std::string arguments_json, OnDone done) {
  auto args_opt = agent::json::Parse(arguments_json);
  if (!args_opt) {
    std::move(done)("", "invalid JSON arguments");
    return;
  }

  auto id = get_string_required(*args_opt, "id");
  auto reason = get_string_required(*args_opt, "reason");
  if (!id) {
    std::move(done)("", "Missing argument: id");
    return;
  }
  if (!reason) {
    std::move(done)("", "Missing argument: reason");
    return;
  }

  const SimpleResult r = plan_->AbandonTask(*id, *reason);
  if (!r.error.empty()) {
    std::move(done)("", r.error);
    return;
  }

  std::move(done)(ok_updated(r.updated), "");
}

PlanRemoveTaskFunction::PlanRemoveTaskFunction(PlanModel* plan) : plan_(plan) {
  spec_.name = "plan.remove_task";
  spec_.description = "Remove a task and its subtree; also removes downstream dependent subtrees.";

  nlohmann::json root = nlohmann::json::object();
  root["type"] = "object";
  root["properties"] = nlohmann::json::object({
      {"id", nlohmann::json::object({{"type", "string"}})},
  });
  root["required"] = nlohmann::json::array({"id"});
  spec_.parameters_json = agent::json::Dump(root);
}

const agent::FunctionSpec& PlanRemoveTaskFunction::spec() const { return spec_; }

void PlanRemoveTaskFunction::InvokeAsync(std::string arguments_json, OnDone done) {
  auto args_opt = agent::json::Parse(arguments_json);
  if (!args_opt) {
    std::move(done)("", "invalid JSON arguments");
    return;
  }

  auto id = get_string_required(*args_opt, "id");
  if (!id) {
    std::move(done)("", "Missing argument: id");
    return;
  }

  const RemoveTaskResult r = plan_->RemoveTask(*id);
  if (!r.error.empty()) {
    std::move(done)("", r.error);
    return;
  }

  std::move(done)(ok_deleted(r.deleted), "");
}

PlanClearSubtasksFunction::PlanClearSubtasksFunction(PlanModel* plan) : plan_(plan) {
  spec_.name = "plan.clear_subtasks";
  spec_.description = "Clear all subtasks under a parent (deletes entire subtask subtree).";

  nlohmann::json root = nlohmann::json::object();
  root["type"] = "object";
  root["properties"] = nlohmann::json::object({
      {"parent_id", nlohmann::json::object({{"type", "string"}})},
  });
  root["required"] = nlohmann::json::array({"parent_id"});
  spec_.parameters_json = agent::json::Dump(root);
}

const agent::FunctionSpec& PlanClearSubtasksFunction::spec() const { return spec_; }

void PlanClearSubtasksFunction::InvokeAsync(std::string arguments_json, OnDone done) {
  auto args_opt = agent::json::Parse(arguments_json);
  if (!args_opt) {
    std::move(done)("", "invalid JSON arguments");
    return;
  }

  auto parent_id = get_string_required(*args_opt, "parent_id");
  if (!parent_id) {
    std::move(done)("", "Missing argument: parent_id");
    return;
  }

  const ClearSubtasksResult r = plan_->ClearSubtasks(*parent_id);
  if (!r.error.empty()) {
    std::move(done)("", r.error);
    return;
  }

  std::move(done)(ok_deleted(r.deleted), "");
}

} // namespace agent::plan2
