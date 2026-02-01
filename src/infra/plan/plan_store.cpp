#include "infra/plan/plan_store.h"

#include "infra/json/json.h"

#include <nlohmann/json.hpp>

#include <fstream>
#include <sstream>

namespace agent {

namespace {

static std::string json_escape(const std::string& s) {
  const std::string dumped = nlohmann::json(s).dump();
  if (dumped.size() < 2)
    return {};
  return dumped.substr(1, dumped.size() - 2);
}

static std::string read_all(const std::filesystem::path& p) {
  std::ifstream ifs(p);
  if (!ifs) return {};
  std::ostringstream oss;
  oss << ifs.rdbuf();
  return oss.str();
}

static Task task_from_json(const nlohmann::json& obj);

static std::vector<Task> tasks_from_json_array(const nlohmann::json& arr) {
  std::vector<Task> out;
  if (!arr.is_array())
    return out;

  out.reserve(arr.size());
  for (const auto& v : arr) {
    if (v.is_object())
      out.push_back(task_from_json(v));
  }
  return out;
}

static Task task_from_json(const nlohmann::json& obj) {
  Task t;
  if (!obj.is_object())
    return t;

  if (auto it = obj.find("goal"); it != obj.end() && it->is_string())
    t.goal = it->get<std::string>();
  if (auto it = obj.find("title"); it != obj.end() && it->is_string())
    t.title = it->get<std::string>();
  if (auto it = obj.find("active"); it != obj.end() && it->is_boolean())
    t.active = it->get<bool>();
  if (auto it = obj.find("completed"); it != obj.end() && it->is_boolean())
    t.completed = it->get<bool>();

  if (auto it = obj.find("history"); it != obj.end() && it->is_array()) {
    for (const auto& h : *it) {
      if (h.is_string())
        t.history.push_back(h.get<std::string>());
    }
  }

  if (auto it = obj.find("children"); it != obj.end() && it->is_array()) {
    t.children = tasks_from_json_array(*it);
  }

  return t;
}

static Plan parse_plan_or_empty(const std::string& json_text) {
  Plan p;
  auto root_opt = agent::json::Parse(json_text);
  if (!root_opt || !root_opt->is_object())
    return p;

  const auto& root = *root_opt;
  if (auto it = root.find("version"); it != root.end() && it->is_number_integer()) {
    p.version = it->get<int>();
  }
  if (auto it = root.find("tasks"); it != root.end() && it->is_array()) {
    p.tasks = tasks_from_json_array(*it);
  }

  return p;
}

static nlohmann::json task_to_json(const Task& t) {
  nlohmann::json obj = nlohmann::json::object();
  obj["goal"] = t.goal;
  obj["title"] = t.title;
  if (t.active)
    obj["active"] = true;
  if (t.completed)
    obj["completed"] = true;

  if (!t.history.empty())
    obj["history"] = t.history;

  if (!t.children.empty()) {
    nlohmann::json children = nlohmann::json::array();
    for (const auto& c : t.children)
      children.push_back(task_to_json(c));
    obj["children"] = std::move(children);
  }
  return obj;
}

static std::string serialize_plan_json(const Plan& p) {
  nlohmann::json root = nlohmann::json::object();
  root["version"] = p.version;

  nlohmann::json tasks = nlohmann::json::array();
  for (const auto& t : p.tasks)
    tasks.push_back(task_to_json(t));
  root["tasks"] = std::move(tasks);

  return agent::json::Dump(root);
}

static void render_task(std::ostringstream& oss,
                        Task& t,
                        const std::string& no,
                        bool bold,
                        const std::vector<const Task*>& active_chain,
                        int indent) {
  std::string pad(static_cast<size_t>(indent) * 3, ' ');
  bool in_chain = false;
  for (auto* p : active_chain) {
    if (p == &t) {
      in_chain = true;
      break;
    }
  }
  bool is_active = t.active;
  bool make_bold = is_active || in_chain;

  oss << pad << no << ". ";
  if (make_bold) oss << "**";
  if (t.completed) oss << "~~";
  oss << t.title;
  if (t.completed) oss << "~~";
  if (make_bold) oss << "**";
  oss << "\n";

  std::string pad2(static_cast<size_t>(indent) * 3 + 3, ' ');
  oss << pad2 << "- goal: " << t.goal << "\n";

  if (!t.history.empty()) {
    oss << pad2 << "- history:" << "\n";
    for (const auto& h : t.history) {
      oss << pad2 << "  - " << h << "\n";
    }
  }

  for (size_t i = 0; i < t.children.size(); ++i) {
    render_task(oss, t.children[i], no + "." + std::to_string(i + 1), false, active_chain, indent + 1);
  }
}

static void build_active_chain(Task& root, std::vector<const Task*>& chain) {
  if (root.active) {
    chain.push_back(&root);
    return;
  }
  for (auto& c : root.children) {
    size_t before = chain.size();
    build_active_chain(c, chain);
    if (chain.size() != before) {
      chain.insert(chain.begin(), &root);
      return;
    }
  }
}

static std::vector<const Task*> compute_active_chain(std::vector<Task>& tasks) {
  std::vector<const Task*> chain;
  for (auto& t : tasks) {
    build_active_chain(t, chain);
    if (!chain.empty()) return chain;
  }
  return chain;
}

} // namespace

PlanStore::PlanStore(std::filesystem::path path) : path_(std::move(path)) {}

void PlanStore::load() {
  std::lock_guard<std::mutex> lock(mu_);
  plan_ = parse_plan_or_empty(read_all(path_));
  ensure_active_leaf_or_clear_locked();
}

Plan PlanStore::snapshot() const {
  std::lock_guard<std::mutex> lock(mu_);
  return plan_;
}

TaskRef PlanStore::find_locked(const TaskNo& no) {
  return find_by_no(plan_, no);
}

std::string PlanStore::render_markdown() {
  std::lock_guard<std::mutex> lock(mu_);
  std::ostringstream oss;
  oss << "# Tasks\n";

  auto chain = compute_active_chain(plan_.tasks);
  for (size_t i = 0; i < plan_.tasks.size(); ++i) {
    render_task(oss, plan_.tasks[i], std::to_string(i + 1), false, chain, 0);
  }

  // Render recently completed root markers (best-effort) after normal tasks.
  // If numbering drifted since completion, the marker may appear under a wrong prefix.
  for (const auto& m : recently_completed_) {
    oss << to_string(m.no) << ". ~~" << m.title << "~~\n";
  }
  recently_completed_.clear();

  return oss.str();
}

static void insert_task(std::vector<Task>& vec, Task t, int after_index) {
  if (after_index < 0 || after_index >= static_cast<int>(vec.size())) {
    vec.push_back(std::move(t));
    return;
  }
  vec.insert(vec.begin() + (after_index + 1), std::move(t));
}

std::string PlanStore::add(const std::optional<TaskNo>& parent_no,
                           const std::string& goal,
                           const std::string& title,
                           const std::optional<TaskNo>& after_no) {
  (void)parent_no;
  std::lock_guard<std::mutex> lock(mu_);

  Task t;
  t.goal = goal;
  t.title = title;

  if (!after_no) {
    // Append to root tasks.
    plan_.tasks.push_back(std::move(t));
    persist_locked();
    return "ok";
  }

  auto ref = find_by_no(plan_, *after_no);
  if (!ref.task) return "after_no not found";

  if (!ref.parent) {
    // Insert after a root task.
    insert_task(plan_.tasks, std::move(t), ref.index);
    persist_locked();
    return "ok";
  }

  // Insert after a non-root task within its parent's children.
  insert_task(ref.parent->children, std::move(t), ref.index);
  persist_locked();
  return "ok";
}

std::string PlanStore::switch_to(const TaskNo& no) {
  std::lock_guard<std::mutex> lock(mu_);
  auto ref = find_by_no(plan_, no);
  if (!ref.task) return "Task not found";

  clear_active(plan_);

  if (ref.task->children.empty()) {
    if (ref.task->completed) return "Task is completed";
    ref.task->active = true;
    persist_locked();
    return "ok";
  }

  auto leaf = first_leaf(*ref.task);
  if (!leaf.task) return "No incomplete leaf task";

  leaf.task->active = true;
  persist_locked();
  return "ok";
}

static bool is_under(Task& root, Task* target) {
  if (&root == target) return true;
  for (auto& c : root.children) {
    if (is_under(c, target)) return true;
  }
  return false;
}

static Task* find_active_ptr(std::vector<Task>& tasks) {
  std::vector<Task*> stack;
  for (auto& t : tasks) stack.push_back(&t);
  while (!stack.empty()) {
    Task* t = stack.back();
    stack.pop_back();
    if (t->active) return t;
    for (auto& c : t->children) stack.push_back(&c);
  }
  return nullptr;
}

static std::optional<TaskRef> first_leaf_in_sibling_range(std::vector<Task>& siblings, int start_idx) {
  for (int j = start_idx; j < static_cast<int>(siblings.size()); ++j) {
    Task& cand = siblings[static_cast<size_t>(j)];
    auto leaf = first_leaf(cand);
    if (leaf.task) return leaf;
  }
  return std::nullopt;
}

static std::optional<TaskRef> last_leaf_in_sibling_range(std::vector<Task>& siblings, int end_idx_inclusive) {
  for (int j = end_idx_inclusive; j >= 0; --j) {
    Task& cand = siblings[static_cast<size_t>(j)];
    auto leaf = last_leaf(cand);
    if (leaf.task) return leaf;
  }
  return std::nullopt;
}

// Implements plan.md active migration rule B.
// path is a 0-based index chain from roots to the (former) active node.
static TaskRef find_leaf_next_to_path(std::vector<Task>& roots, const std::vector<int>& path) {
  if (path.empty()) return {};

  std::vector<Task>* siblings = &roots;

  // Build ancestor chain of sibling vectors.
  std::vector<std::vector<Task>*> sibling_chain;
  std::vector<int> idx_chain;

  siblings = &roots;
  for (size_t depth = 0; depth < path.size(); ++depth) {
    int idx = path[depth];
    if (idx < 0 || idx >= static_cast<int>(siblings->size())) break;
    sibling_chain.push_back(siblings);
    idx_chain.push_back(idx);
    Task& cur = (*siblings)[static_cast<size_t>(idx)];
    siblings = &cur.children;
  }

  // Try at each ancestor level from deepest parent upwards.
  for (size_t back = idx_chain.size(); back-- > 0;) {
    auto* sib = sibling_chain[back];
    int idx = idx_chain[back];

    // 1) after
    if (auto r = first_leaf_in_sibling_range(*sib, idx + 1)) return *r;

    // 2) before
    if (auto r = last_leaf_in_sibling_range(*sib, idx - 1)) return *r;

    // continue ascending
  }

  return {};
}

static bool build_path_to_active(std::vector<Task>& roots, Task* active, std::vector<int>& out) {
  for (size_t i = 0; i < roots.size(); ++i) {
    if (&roots[i] == active) {
      out.push_back(static_cast<int>(i));
      return true;
    }
    out.push_back(static_cast<int>(i));
    if (build_path_to_active(roots[i].children, active, out)) return true;
    out.pop_back();
  }
  return false;
}

void PlanStore::migrate_active_after_deletion_locked() {
  Task* prev_active = find_active_ptr(plan_.tasks);
  if (prev_active) {
    // If still exists, keep.
    if (prev_active->children.empty()) return;
  }

  // Find any leaf.
  for (auto& t : plan_.tasks) {
    auto leaf = first_leaf(t);
    if (leaf.task) {
      clear_active(plan_);
      leaf.task->active = true;
      return;
    }
  }
  clear_active(plan_);
}

std::string PlanStore::complete(const TaskNo& no) {
  std::lock_guard<std::mutex> lock(mu_);

  auto ref = find_by_no(plan_, no);
  if (!ref.task) return "Task not found";

  if (!ref.parent) {
    // Root task: mark completed and delete immediately.
    CompletedMarker m;
    m.no = no;
    m.title = ref.task->title;
    recently_completed_.push_back(std::move(m));

    plan_.tasks.erase(plan_.tasks.begin() + ref.index);

    // If active was inside this subtree, clear and then pick any remaining leaf.
    clear_active(plan_);
    ensure_active_leaf_or_clear_locked();

    persist_locked();
    return "ok";
  }

  // Non-root task: mark completed in-place.
  ref.task->completed = true;

  // If we completed the active leaf, migrate active according to rule B.
  if (ref.task->active) {
    std::vector<int> active_path;
    Task* active_ptr = ref.task;
    build_path_to_active(plan_.tasks, active_ptr, active_path);

    // Mark inactive before computing the fallback leaf (find_leaf_next_to_path relies on the path).
    clear_active(plan_);

    if (!active_path.empty()) {
      // Prefer siblings under the same parent; use the parent's sibling vector.
      if (active_path.size() >= 2) {
        active_path.pop_back();
      }

      auto alt = find_leaf_next_to_path(plan_.tasks, active_path);
      if (alt.task) alt.task->active = true;
    }
  }

  ensure_active_leaf_or_clear_locked();

  persist_locked();
  return "ok";
}

std::string PlanStore::replan(const TaskNo& no,
                             std::vector<Task> new_children,
                             const std::string& history_line) {
  std::lock_guard<std::mutex> lock(mu_);

  // Track current active path and whether active is within the subtree.
  Task* active_ptr = find_active_ptr(plan_.tasks);
  std::vector<int> active_path;
  if (active_ptr) {
    build_path_to_active(plan_.tasks, active_ptr, active_path);
  }

  auto ref = find_by_no(plan_, no);
  if (!ref.task) return "Task not found";

  bool active_in_old_subtree = false;
  if (active_ptr) {
    active_in_old_subtree = is_under(*ref.task, active_ptr);
  }

  ref.task->history.push_back(history_line);
  ref.task->children = std::move(new_children);

  if (active_in_old_subtree) {
    clear_active(plan_);

    // Prefer first leaf in new subtree.
    auto leaf = first_leaf(*ref.task);
    if (leaf.task && leaf.task->children.empty()) {
      leaf.task->active = true;
    } else {
      // Fall back to global search near previous active path.
      if (!active_path.empty()) {
        auto alt = find_leaf_next_to_path(plan_.tasks, active_path);
        if (alt.task) alt.task->active = true;
      }
    }
  }

  ensure_active_leaf_or_clear_locked();
  persist_locked();
  return "ok";
}

void PlanStore::ensure_active_leaf_or_clear_locked() {
  // Ensure at most one active, and active must be an incomplete leaf.
  Task* found = nullptr;
  std::vector<Task*> stack;
  for (auto& t : plan_.tasks) stack.push_back(&t);
  while (!stack.empty()) {
    Task* t = stack.back();
    stack.pop_back();
    if (t->active) {
      if (!found) {
        if (t->children.empty() && !t->completed) {
          found = t;
        } else {
          t->active = false;
        }
      } else {
        t->active = false;
      }
    }
    for (auto& c : t->children) stack.push_back(&c);
  }

  if (!found) {
    // Pick the first incomplete leaf.
    for (auto& t : plan_.tasks) {
      auto leaf = first_leaf(t);
      if (leaf.task && !leaf.task->completed) {
        clear_active(plan_);
        leaf.task->active = true;
        return;
      }
    }
    clear_active(plan_);
    return;
  }
}

void PlanStore::persist_locked() {
  std::error_code ec;
  std::filesystem::create_directories(path_.parent_path(), ec);

  std::ofstream ofs(path_);
  if (!ofs) {
    // Best-effort: keep running without persistence.
    return;
  }
  ofs << serialize_plan_json(plan_);
}

}  // namespace agent
