#pragma once

#include <optional>
#include <string>
#include <vector>

namespace cpp_agent::infra::plan {

struct Task {
  std::string goal;
  std::string title;
  std::vector<std::string> history;
  std::vector<Task> children;
  bool active{false};
};

struct Plan {
  int version{1};
  std::vector<Task> tasks;
};

// A positional address like 1.2.3 (1-based indices).
struct TaskNo {
  std::vector<int> parts;
};

[[nodiscard]] std::optional<TaskNo> parse_task_no(const std::string& no);
[[nodiscard]] std::string to_string(const TaskNo& no);

// Returns the (task pointer, parent pointer, index in parent/tasks) triple.
struct TaskRef {
  Task* task{nullptr};
  Task* parent{nullptr}; // nullptr means root list
  int index{-1};
};

[[nodiscard]] TaskRef find_by_no(Plan& plan, const TaskNo& no);
[[nodiscard]] bool is_leaf(const Task& t);

// Active handling
[[nodiscard]] TaskRef find_active_leaf(Plan& plan);
void clear_active(Plan& plan);

// DFS utilities
[[nodiscard]] TaskRef first_leaf(Task& root);
[[nodiscard]] TaskRef last_leaf(Task& root);

} // namespace cpp_agent::infra::plan
