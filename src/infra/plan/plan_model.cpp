#include "infra/plan/plan_model.h"

#include <cctype>

namespace agent {

std::optional<TaskNo> parse_task_no(const std::string& no) {
  TaskNo out;
  int cur = 0;
  bool have = false;
  for (size_t i = 0; i < no.size(); ++i) {
    unsigned char c = static_cast<unsigned char>(no[i]);
    if (std::isdigit(c)) {
      have = true;
      cur = cur * 10 + (no[i] - '0');
      continue;
    }
    if (no[i] == '.') {
      if (!have || cur <= 0) return std::nullopt;
      out.parts.push_back(cur);
      cur = 0;
      have = false;
      continue;
    }
    return std::nullopt;
  }
  if (!have || cur <= 0) return std::nullopt;
  out.parts.push_back(cur);
  return out;
}

std::string to_string(const TaskNo& no) {
  std::string out;
  for (size_t i = 0; i < no.parts.size(); ++i) {
    if (i) out.push_back('.');
    out += std::to_string(no.parts[i]);
  }
  return out;
}

static TaskRef find_in_children(std::vector<Task>& children, const std::vector<int>& parts, size_t idx) {
  if (idx >= parts.size()) return {};
  int one_based = parts[idx];
  if (one_based <= 0) return {};
  int zero = one_based - 1;
  if (zero >= static_cast<int>(children.size())) return {};

  Task& t = children[static_cast<size_t>(zero)];
  if (idx + 1 == parts.size()) {
    return TaskRef{&t, nullptr, zero};
  }

  auto child_ref = find_in_children(t.children, parts, idx + 1);
  if (!child_ref.task) return {};
  if (!child_ref.parent) child_ref.parent = &t;
  return child_ref;
}

TaskRef find_by_no(Plan& plan, const TaskNo& no) {
  if (no.parts.empty()) return {};
  int root_one = no.parts[0];
  if (root_one <= 0) return {};
  int root_zero = root_one - 1;
  if (root_zero >= static_cast<int>(plan.tasks.size())) return {};

  Task& root = plan.tasks[static_cast<size_t>(root_zero)];
  if (no.parts.size() == 1) {
    return TaskRef{&root, nullptr, root_zero};
  }

  // Find deeper. We'll use parent pointer to indicate actual parent.
  // We return task pointer and parent pointer and index.
  Task* parent = &root;
  std::vector<Task>* vec = &root.children;
  int index = -1;
  Task* task = nullptr;

  for (size_t i = 1; i < no.parts.size(); ++i) {
    int one = no.parts[i];
    if (one <= 0) return {};
    int zero = one - 1;
    if (zero >= static_cast<int>(vec->size())) return {};
    index = zero;
    task = &(*vec)[static_cast<size_t>(zero)];
    if (i + 1 < no.parts.size()) {
      parent = task;
      vec = &task->children;
    }
  }

  if (!task) return {};

  // parent should already be set to the direct parent for the last step.
  return TaskRef{task, parent, index};
}

bool is_leaf(const Task& t) { return t.children.empty(); }

static TaskRef first_leaf_in_vec(std::vector<Task>& vec) {
  for (size_t i = 0; i < vec.size(); ++i) {
    Task& t = vec[i];
    if (t.children.empty()) {
      return TaskRef{&t, nullptr, static_cast<int>(i)};
    }
    auto r = first_leaf_in_vec(t.children);
    if (r.task) {
      if (!r.parent) r.parent = &t;
      return r;
    }
  }
  return {};
}

static TaskRef first_incomplete_leaf_in_vec(std::vector<Task>& vec) {
  for (size_t i = 0; i < vec.size(); ++i) {
    Task& t = vec[i];
    if (t.children.empty()) {
      if (!t.completed) return TaskRef{&t, nullptr, static_cast<int>(i)};
      continue;
    }
    auto r = first_incomplete_leaf_in_vec(t.children);
    if (r.task) {
      if (!r.parent) r.parent = &t;
      return r;
    }
  }
  return {};
}

TaskRef first_leaf(Task& root) {
  if (root.children.empty()) {
    return root.completed ? TaskRef{} : TaskRef{&root, nullptr, 0};
  }
  auto r = first_incomplete_leaf_in_vec(root.children);
  if (r.task && !r.parent) r.parent = &root;
  return r;
}

static TaskRef last_leaf_in_vec(std::vector<Task>& vec) {
  for (size_t i = vec.size(); i-- > 0;) {
    Task& t = vec[i];
    if (t.children.empty()) {
      return TaskRef{&t, nullptr, static_cast<int>(i)};
    }
    auto r = last_leaf_in_vec(t.children);
    if (r.task) {
      if (!r.parent) r.parent = &t;
      return r;
    }
  }
  return {};
}

TaskRef last_leaf(Task& root) {
  if (root.children.empty()) return TaskRef{&root, nullptr, 0};
  auto r = last_leaf_in_vec(root.children);
  if (r.task && !r.parent) r.parent = &root;
  return r;
}

void clear_active(Plan& plan) {
  std::vector<Task*> stack;
  for (auto& t : plan.tasks) stack.push_back(&t);
  while (!stack.empty()) {
    Task* t = stack.back();
    stack.pop_back();
    t->active = false;
    for (auto& c : t->children) stack.push_back(&c);
  }
}

TaskRef find_active_leaf(Plan& plan) {
  std::vector<Task*> stack;
  for (auto& t : plan.tasks) stack.push_back(&t);
  while (!stack.empty()) {
    Task* t = stack.back();
    stack.pop_back();
    if (t->active && t->children.empty()) {
      return TaskRef{t, nullptr, 0};
    }
    for (auto& c : t->children) stack.push_back(&c);
  }
  return {};
}

} // namespace agent
