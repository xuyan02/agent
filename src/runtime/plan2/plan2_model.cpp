#include "runtime/plan2/plan2_model.h"
#include "runtime/plan2/uuid_gen.h"

#include <algorithm>
#include <functional>
#include <sstream>

namespace agent::plan2 {
namespace {

std::string StatusToString(Status s) {
  switch (s) {
    case Status::kPending:
      return "pending";
    case Status::kNotReady:
      return "not_ready";
    case Status::kBlocked:
      return "blocked";
    case Status::kInProgress:
      return "in_progress";
    case Status::kDone:
      return "done";
    case Status::kCanceled:
      return "canceled";
    case Status::kFailed:
      return "failed";
  }
  return "unknown";
}

} // namespace

PlanModel::PlanModel() = default;

const Task* PlanModel::Find(const std::string& id) const {
  const auto it = tasks_.find(id);
  if (it == tasks_.end()) return nullptr;
  return &it->second;
}

std::vector<std::string> PlanModel::ListIdsInInsertionOrder() const { return insertion_order_; }

bool PlanModel::Exists(const std::string& id) const { return tasks_.find(id) != tasks_.end(); }

bool PlanModel::IsTopLevel(const Task& t) const { return !t.parent_id.has_value(); }

bool PlanModel::IsTerminal(Status s) const {
  return s == Status::kDone || s == Status::kCanceled || s == Status::kFailed;
}

bool PlanModel::DepsSatisfied(const Task& t) const {
  for (const auto& dep : t.depends_on) {
    const auto it = tasks_.find(dep);
    if (it == tasks_.end()) return false;
    if (it->second.status != Status::kDone) return false;
  }
  return true;
}

bool PlanModel::ValidateDependsOnSiblings(const AddTaskInput& in, std::string* out_error) const {
  const std::optional<std::string> parent_id = in.parent_id;
  for (const auto& dep : in.depends_on) {
    const auto it = tasks_.find(dep);
    if (it == tasks_.end()) {
      *out_error = "depends_on references missing task: " + dep;
      return false;
    }
    if (it->second.parent_id != parent_id) {
      *out_error = "depends_on must reference sibling tasks (same parent_id)";
      return false;
    }
  }
  return true;
}

bool PlanModel::ValidateNoDependencyCycle(const AddTaskInput& in, std::string* out_error) const {
  // Sibling-only dependency graph. Adding a new node cannot create a cycle unless
  // it depends (directly or indirectly) on itself; since it's new, we only need
  // to prevent it from depending on any node that already reaches back to any of
  // its dependencies via existing edges.
  //
  // Simplification: check that none of the dependencies (or their transitive
  // dependencies) includes any other dependency that would form a cycle with the
  // new node. Because the new node has no inbound edges yet, this reduces to:
  // - ensure no duplicate in.depends_on
  // - ensure no dep depends (transitively) on another dep in a way that would
  //   require the new node to be on the path (impossible).
  //
  // For correctness and future-proofing, we implement a general DFS that checks
  // whether any dependency reaches any other dependency. This is always safe and
  // catches existing cycles too.

  std::unordered_set<std::string> uniq;
  for (const auto& d : in.depends_on) {
    if (!uniq.insert(d).second) {
      *out_error = "depends_on contains duplicates";
      return false;
    }
  }

  // Gather sibling set.
  std::vector<std::string> siblings;
  if (in.parent_id.has_value()) {
    const auto it = children_.find(*in.parent_id);
    if (it != children_.end()) siblings = it->second;
  } else {
    siblings = top_level_;
  }

  std::unordered_set<std::string> sibling_set(siblings.begin(), siblings.end());

  // Build adjacency among siblings from existing tasks.
  auto neighbors = [&](const std::string& id) -> const std::vector<std::string>* {
    const auto it = tasks_.find(id);
    if (it == tasks_.end()) return nullptr;
    return &it->second.depends_on;
  };

  // Detect any cycle among existing siblings (defensive).
  enum class Mark { kTemp, kPerm };
  std::unordered_map<std::string, Mark> marks;

  std::function<bool(const std::string&)> dfs_cycle = [&](const std::string& n) {
    const auto mit = marks.find(n);
    if (mit != marks.end()) {
      return mit->second == Mark::kTemp;
    }
    marks.emplace(n, Mark::kTemp);
    const auto* deps = neighbors(n);
    if (deps) {
      for (const auto& d : *deps) {
        if (sibling_set.find(d) == sibling_set.end()) continue;
        if (dfs_cycle(d)) return true;
      }
    }
    marks[n] = Mark::kPerm;
    return false;
  };

  for (const auto& s : siblings) {
    if (dfs_cycle(s)) {
      *out_error = "dependency cycle detected";
      return false;
    }
  }

  return true;
}

bool PlanModel::ValidateReportToRule(const AddTaskInput& in, std::string* out_error) const {
  if (!in.parent_id.has_value()) {
    if (!in.report_to.has_value() || in.report_to->empty()) {
      *out_error = "top-level task requires report_to";
      return false;
    }
    return true;
  }

  if (in.report_to.has_value()) {
    *out_error = "non-top-level task must not have report_to";
    return false;
  }
  return true;
}

Status PlanModel::ComputeInitialStatus(const AddTaskInput& in) const {
  // If dependencies exist and are not satisfied, the task starts not_ready.
  if (!in.depends_on.empty()) {
    for (const auto& dep : in.depends_on) {
      const auto it = tasks_.find(dep);
      if (it == tasks_.end() || it->second.status != Status::kDone) {
        return Status::kNotReady;
      }
    }
  }
  return Status::kPending;
}

AddTasksResult PlanModel::AddTasks(const std::vector<AddTaskInput>& inputs) {
  AddTasksResult out;

  for (const auto& in : inputs) {
    std::string error;
    if (!ValidateReportToRule(in, &error)) {
      out.error = error;
      return out;
    }

    if (in.title.empty()) {
      out.error = "title is required";
      return out;
    }

    if (in.parent_id.has_value() && !Exists(*in.parent_id)) {
      out.error = "parent_id not found";
      return out;
    }

    if (!ValidateDependsOnSiblings(in, &error)) {
      out.error = error;
      return out;
    }

    if (!ValidateNoDependencyCycle(in, &error)) {
      out.error = error;
      return out;
    }

    const std::string id = GenerateUuidV4();

    Task t;
    t.id = id;
    t.title = in.title;
    t.detail = in.detail;
    t.parent_id = in.parent_id;
    t.depends_on = in.depends_on;
    t.report_to = in.report_to;
    t.status = ComputeInitialStatus(in);

    insertion_order_.push_back(id);
    tasks_.emplace(id, t);

    if (t.parent_id.has_value()) {
      children_[*t.parent_id].push_back(id);
    } else {
      top_level_.push_back(id);
    }

    out.created.push_back(t);
  }

  return out;
}

void PlanModel::NormalizeSiblings(const std::optional<std::string>& parent_id, SetStatusResult* inout) {
  // Normalize only tasks that are currently pending/not_ready.
  auto normalize_one = [&](const std::string& id) {
    auto it = tasks_.find(id);
    if (it == tasks_.end()) return;
    Task& t = it->second;
    if (IsTerminal(t.status) || t.status == Status::kInProgress || t.status == Status::kBlocked) return;

    const bool deps_ok = DepsSatisfied(t);
    const Status want = deps_ok ? Status::kPending : Status::kNotReady;
    if (t.status != want) {
      t.status = want;
      inout->normalized.push_back(t);
    }
  };

  if (parent_id.has_value()) {
    const auto it = children_.find(*parent_id);
    if (it == children_.end()) return;
    for (const auto& id : it->second) {
      normalize_one(id);
    }
    return;
  }

  for (const auto& id : top_level_) {
    normalize_one(id);
  }
}

std::vector<std::string> PlanModel::DirectDependentsOf(const std::string& task_id) const {
  std::vector<std::string> out;
  const auto it = tasks_.find(task_id);
  if (it == tasks_.end()) return out;

  const std::optional<std::string> parent = it->second.parent_id;

  const std::vector<std::string>* siblings = nullptr;
  if (parent.has_value()) {
    const auto sit = children_.find(*parent);
    if (sit != children_.end()) siblings = &sit->second;
  } else {
    siblings = &top_level_;
  }

  if (!siblings) return out;

  for (const auto& sid : *siblings) {
    if (sid == task_id) continue;
    const auto jt = tasks_.find(sid);
    if (jt == tasks_.end()) continue;
    for (const auto& dep : jt->second.depends_on) {
      if (dep == task_id) {
        out.push_back(sid);
        break;
      }
    }
  }
  return out;
}

std::vector<std::string> PlanModel::TransitiveDependentsClosure(const std::string& task_id) const {
  std::vector<std::string> out;
  std::unordered_set<std::string> seen;

  std::vector<std::string> stack = DirectDependentsOf(task_id);
  while (!stack.empty()) {
    std::string cur = std::move(stack.back());
    stack.pop_back();
    if (!seen.insert(cur).second) continue;
    out.push_back(cur);

    auto next = DirectDependentsOf(cur);
    for (auto& n : next) stack.push_back(std::move(n));
  }

  return out;
}

std::vector<std::string> PlanModel::CollectSubtreeIds(const std::string& task_id) const {
  std::vector<std::string> out;
  std::vector<std::string> stack;
  stack.push_back(task_id);

  while (!stack.empty()) {
    const std::string cur = std::move(stack.back());
    stack.pop_back();

    const auto it = tasks_.find(cur);
    if (it == tasks_.end()) continue;

    out.push_back(cur);

    const auto cit = children_.find(cur);
    if (cit != children_.end()) {
      for (const auto& c : cit->second) {
        stack.push_back(c);
      }
    }
  }

  return out;
}

void PlanModel::HardDeleteSubtree(const std::string& task_id, std::vector<std::string>* deleted) {
  const auto ids = CollectSubtreeIds(task_id);
  if (ids.empty()) return;

  // Remove from parent children vector or top-level.
  const auto it = tasks_.find(task_id);
  if (it != tasks_.end()) {
    const auto parent = it->second.parent_id;
    if (parent.has_value()) {
      auto& v = children_[*parent];
      v.erase(std::remove(v.begin(), v.end(), task_id), v.end());
    } else {
      top_level_.erase(std::remove(top_level_.begin(), top_level_.end(), task_id), top_level_.end());
    }
  }

  // Delete all nodes.
  for (const auto& id : ids) {
    children_.erase(id);
    tasks_.erase(id);
    insertion_order_.erase(std::remove(insertion_order_.begin(), insertion_order_.end(), id), insertion_order_.end());
    deleted->push_back(id);
  }

  // Clean depends_on references.
  std::unordered_set<std::string> deleted_set(deleted->begin(), deleted->end());
  for (auto& [id, t] : tasks_) {
    t.depends_on.erase(std::remove_if(t.depends_on.begin(), t.depends_on.end(), [&](const std::string& d) {
                       return deleted_set.find(d) != deleted_set.end();
                     }),
                     t.depends_on.end());
  }
}

void PlanModel::CascadingCancelDependents(const std::string& task_id, SetStatusResult* out) {
  const auto deps = TransitiveDependentsClosure(task_id);
  for (const auto& dep_id : deps) {
    auto it = tasks_.find(dep_id);
    if (it == tasks_.end()) continue;
    Task& t = it->second;
    if (IsTerminal(t.status)) continue;
    t.status = Status::kCanceled;
    t.block_reason.clear();
    out->canceled_cascade.push_back(t);
  }
}

SetStatusResult PlanModel::SetStatus(const std::string& task_id, Status status, std::string block_reason) {
  SetStatusResult out;

  auto it = tasks_.find(task_id);
  if (it == tasks_.end()) {
    out.error = "task_id not found";
    return out;
  }

  Task& t = it->second;

  if (IsTerminal(t.status)) {
    out.error = "terminal status cannot roll back";
    return out;
  }

  const bool deps_ok = DepsSatisfied(t);

  if (status == Status::kPending && !deps_ok) {
    out.error = "unmet_dependencies";
    return out;
  }

  if (status == Status::kNotReady && deps_ok) {
    out.error = "dependencies_already_satisfied";
    return out;
  }

  if (status == Status::kBlocked) {
    if (!deps_ok) {
      out.error = "unmet_dependencies";
      return out;
    }
    if (block_reason.empty()) {
      out.error = "block_reason is required";
      return out;
    }
  } else {
    if (!block_reason.empty()) {
      out.error = "block_reason must be empty unless status is blocked";
      return out;
    }
  }

  if (status == Status::kInProgress && !deps_ok) {
    out.error = "unmet_dependencies";
    return out;
  }

  // Apply.
  t.status = status;
  t.block_reason = (status == Status::kBlocked) ? block_reason : std::string{};
  out.updated.push_back(t);

  // Cascading cancels.
  if (status == Status::kCanceled || status == Status::kFailed) {
    CascadingCancelDependents(task_id, &out);
  }

  // Parent terminal deletes subtree.
  if (IsTerminal(status)) {
    const auto cit = children_.find(task_id);
    if (cit != children_.end() && !cit->second.empty()) {
      // Hard-delete subtree (children and descendants) but keep the parent itself.
      // Deleting descendants first makes id stability in logs simpler.
      for (const auto& child : std::vector<std::string>(cit->second)) {
        HardDeleteSubtree(child, &out.deleted);
      }
    }
  }

  // Normalize siblings of this task (based on its parent_id).
  NormalizeSiblings(t.parent_id, &out);

  return out;
}

RemoveTaskResult PlanModel::RemoveTask(const std::string& task_id) {
  RemoveTaskResult out;

  if (!Exists(task_id)) {
    out.error = "task_id not found";
    return out;
  }

  // Downstream dependents closure.
  const auto deps = TransitiveDependentsClosure(task_id);

  // Delete root subtree.
  HardDeleteSubtree(task_id, &out.deleted);

  // Delete dependents subtrees.
  for (const auto& dep : deps) {
    if (Exists(dep)) {
      HardDeleteSubtree(dep, &out.deleted);
    }
  }

  return out;
}

std::string PlanModel::RenderMarkdown() const {
  std::ostringstream out;
  out << "## Plan\n";

  std::function<void(const std::string&, int)> render_task = [&](const std::string& id, int depth) {
    const auto it = tasks_.find(id);
    if (it == tasks_.end()) return;
    const Task& t = it->second;

    const int header_indent = 2 * depth;
    const int field_indent = header_indent + 4;

    out << std::string(header_indent, ' ') << "- [" << StatusToString(t.status) << "] " << t.title << "\n";

    out << std::string(field_indent, ' ') << "- id: " << t.id << "\n";
    if (!t.parent_id.has_value()) {
      if (t.report_to.has_value()) {
        out << std::string(field_indent, ' ') << "- report_to: " << *t.report_to << "\n";
      }
    }

    if (t.depends_on.empty()) {
      out << std::string(field_indent, ' ') << "- depends_on: -\n";
    } else {
      out << std::string(field_indent, ' ') << "- depends_on: ";
      for (size_t i = 0; i < t.depends_on.size(); ++i) {
        if (i) out << ", ";
        out << t.depends_on[i];
      }
      out << "\n";
    }

    if (!t.detail.empty()) {
      out << std::string(field_indent, ' ') << "- detail: " << t.detail << "\n";
    }

    if (t.status == Status::kBlocked) {
      out << std::string(field_indent, ' ') << "- block_reason: " << t.block_reason << "\n";
    }

    const auto cit = children_.find(t.id);
    if (cit != children_.end() && !cit->second.empty()) {
      out << std::string(field_indent, ' ') << "- children:\n";
      for (const auto& child_id : cit->second) {
        render_task(child_id, depth + 4);
      }
    }
  };

  for (const auto& id : top_level_) {
    render_task(id, 0);
  }

  return out.str();
}

} // namespace agent::plan2
