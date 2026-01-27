#pragma once

#include <optional>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace agent::plan2 {

enum class Status {
  kPending,
  kNotReady,
  kBlocked,
  kInProgress,
  kDone,
  kCanceled,
  kFailed,
};

struct Task {
  std::string id; // UUID

  std::string title;
  std::string detail;

  std::optional<std::string> parent_id; // UUID
  std::vector<std::string> depends_on;  // UUID list (siblings only)

  // Required for top-level tasks only. Forbidden for non-top-level tasks.
  std::optional<std::string> report_to;

  Status status{Status::kPending};
  std::string block_reason; // required iff status == kBlocked
};

struct AddTaskInput {
  std::string title;
  std::string detail;

  std::optional<std::string> parent_id;
  std::vector<std::string> depends_on;

  std::optional<std::string> report_to;
};

struct AddTasksResult {
  std::vector<Task> created;
  std::vector<std::string> warnings;
  std::string error;
};

struct SetStatusResult {
  std::vector<Task> updated;
  std::vector<Task> canceled_cascade;
  std::vector<std::string> deleted;
  std::vector<Task> normalized;
  std::vector<std::string> warnings;
  std::string error;
};

struct RemoveTaskResult {
  std::vector<std::string> deleted;
  std::vector<std::string> warnings;
  std::string error;
};

class PlanModel {
public:
  PlanModel();

  AddTasksResult AddTasks(const std::vector<AddTaskInput>& inputs);
  SetStatusResult SetStatus(const std::string& task_id, Status status, std::string block_reason);
  RemoveTaskResult RemoveTask(const std::string& task_id);

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

  void NormalizeSiblings(const std::optional<std::string>& parent_id,
                         SetStatusResult* inout);

  std::vector<std::string> DirectDependentsOf(const std::string& task_id) const;
  std::vector<std::string> TransitiveDependentsClosure(const std::string& task_id) const;

  std::vector<std::string> CollectSubtreeIds(const std::string& task_id) const;
  void HardDeleteSubtree(const std::string& task_id, std::vector<std::string>* deleted);

  void CascadingCancelDependents(const std::string& task_id, SetStatusResult* out);

  // Insertion order of tasks (all tasks, including subtasks).
  std::vector<std::string> insertion_order_;

  // Task storage.
  std::unordered_map<std::string, Task> tasks_;

  // Children index: parent_id -> children ids (in insertion order).
  std::unordered_map<std::string, std::vector<std::string>> children_;
  std::vector<std::string> top_level_; // top-level ids in insertion order
};

} // namespace agent::plan2
