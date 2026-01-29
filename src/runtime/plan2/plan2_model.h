#pragma once

#include <optional>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace agent::plan2 {

enum class Status {
  // Blocked by dependency constraints (depends_on not satisfied).
  kBlocked,

  // Ready to be worked on.
  kReady,

  // Currently executing.
  kActive,

  // Explicitly waiting for external info/artifact.
  kWaiting,

  // Terminal.
  kDone,
  kAbandoned,
};

std::string StatusToString(Status s);

struct Task {
  std::string id; // UUID

  std::string name;
  std::string description;
  std::string goal;

  std::optional<std::string> parent_id; // UUID
  std::vector<std::string> depends_on;  // UUID list (siblings only)

  // Required for top-level tasks only. Forbidden for non-top-level tasks.
  std::optional<std::string> report_to;

  Status status{Status::kReady};

  // Required iff status == kWaiting or status == kAbandoned.
  std::string reason;
};

struct AddTaskInput {
  std::string name;
  std::string description;
  std::string goal;

  std::optional<std::string> parent_id;
  std::vector<std::string> depends_on;

  std::optional<std::string> report_to;
};

struct AddTaskResult {
  Task created;
  std::vector<std::string> warnings;
  std::string error;
};

struct AddSubtaskInput {
  std::string parent_id; // required

  std::string name;
  std::string description;
  std::string goal;

  std::vector<std::string> depends_on;
};

struct AddSubtaskResult {
  Task created;
  std::vector<std::string> warnings;
  std::string error;
};

struct ActivateResult {
  std::vector<Task> updated;
  std::vector<std::string> warnings;
  std::string error;
};

struct SimpleResult {
  std::vector<Task> updated;
  std::vector<std::string> warnings;
  std::string error;
};

struct RemoveTaskResult {
  std::vector<std::string> deleted;
  std::vector<std::string> warnings;
  std::string error;
};

struct ClearSubtasksResult {
  std::vector<std::string> deleted;
  std::vector<std::string> warnings;
  std::string error;
};

class PlanModel {
public:
  PlanModel();

  AddTaskResult AddTask(const AddTaskInput& in);
  AddSubtaskResult AddSubtask(const AddSubtaskInput& in);

  ActivateResult ActivateTask(const std::string& task_id);
  SimpleResult SuspendTask(const std::string& task_id, std::string reason);
  SimpleResult ResumeTask(const std::string& task_id);
  SimpleResult CompleteTask(const std::string& task_id);
  SimpleResult AbandonTask(const std::string& task_id, std::string reason);

  RemoveTaskResult RemoveTask(const std::string& task_id);
  ClearSubtasksResult ClearSubtasks(const std::string& parent_id);

  // Rendering for system prompt.
  std::string RenderMarkdown() const;

  // Accessors for tests.
  const Task* Find(const std::string& id) const;
  std::vector<std::string> ListIdsInInsertionOrder() const;

private:
  bool Exists(const std::string& id) const;
  bool IsTopLevel(const Task& t) const;
  bool IsTerminal(Status s) const;
  bool DepsSatisfied(const Task& t) const;

  bool ValidateDependsOnSiblings(const AddTaskInput& in, std::string* out_error) const;
  bool ValidateNoDependencyCycle(const AddTaskInput& in, std::string* out_error) const;
  bool ValidateReportToRule(const AddTaskInput& in, std::string* out_error) const;

  Status ComputeInitialStatus(const AddTaskInput& in) const;

  void NormalizeSiblings(const std::optional<std::string>& parent_id, std::vector<Task>* out_updated);

  std::vector<std::string> DirectDependentsOf(const std::string& task_id) const;
  std::vector<std::string> TransitiveDependentsClosure(const std::string& task_id) const;

  std::vector<std::string> CollectSubtreeIds(const std::string& task_id) const;
  void HardDeleteSubtree(const std::string& task_id, std::vector<std::string>* deleted);


  // Insertion order of tasks (all tasks, including subtasks).
  std::vector<std::string> insertion_order_;

  // Task storage.
  std::unordered_map<std::string, Task> tasks_;

  // Children index: parent_id -> children ids (in insertion order).
  std::unordered_map<std::string, std::vector<std::string>> children_;
  std::vector<std::string> top_level_; // top-level ids in insertion order
};

} // namespace agent::plan2
