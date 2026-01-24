#pragma once

#include "infra/plan/plan_model.h"

#include <filesystem>
#include <mutex>

namespace cpp_agent::infra::plan {

class PlanStore final {
public:
  explicit PlanStore(std::filesystem::path path);

  // Loads from disk if present; otherwise starts empty.
  void load();

  // In-memory state (thread-safe).
  [[nodiscard]] Plan snapshot() const;

  // Mutating operations.
  std::string render_markdown();
  std::string add(const std::optional<TaskNo>& parent_no,
                  const std::string& goal,
                  const std::string& title,
                  const std::optional<TaskNo>& after_no);
  std::string active(const TaskNo& no);
  std::string complete(const TaskNo& no);
  std::string replan(const TaskNo& no,
                     std::vector<Task> new_children,
                     const std::string& history_line);

private:
  void persist_locked();

  // Helpers
  [[nodiscard]] TaskRef find_locked(const TaskNo& no);
  [[nodiscard]] TaskRef find_parent_and_vec_locked(const TaskNo& no, std::vector<Task>** out_vec);
  void ensure_active_leaf_or_clear_locked();
  void migrate_active_after_deletion_locked();

  std::filesystem::path path_;
  mutable std::mutex mu_;
  Plan plan_;

  struct CompletedMarker {
    // Positional address at the moment of completion (best-effort).
    TaskNo no;
    std::string title;
    int ttl{1};
  };

  // Recently completed markers to render once (best-effort).
  std::vector<CompletedMarker> recently_completed_;
};

} // namespace cpp_agent::infra::plan
