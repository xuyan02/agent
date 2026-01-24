#include "infra/plan/plan_store.h"

#include "core/errors.h"

#include <fstream>
#include <sstream>

namespace cpp_agent::infra::plan {

namespace {

static std::string json_escape(const std::string& s) {
  std::string out;
  out.reserve(s.size() + 8);
  for (char c : s) {
    switch (c) {
      case '"': out += "\\\""; break;
      case '\\': out += "\\\\"; break;
      case '\n': out += "\\n"; break;
      case '\r': out += "\\r"; break;
      case '\t': out += "\\t"; break;
      default: out.push_back(c); break;
    }
  }
  return out;
}

static std::string read_all(const std::filesystem::path& p) {
  std::ifstream ifs(p);
  if (!ifs) return {};
  std::ostringstream oss;
  oss << ifs.rdbuf();
  return oss.str();
}

// Extremely small JSON reader for our fixed schema. We only support strings, arrays, and objects.
// This is intentionally strict and best-effort.
static size_t skip_ws(const std::string& s, size_t i) {
  while (i < s.size() && (s[i] == ' ' || s[i] == '\n' || s[i] == '\r' || s[i] == '\t')) i++;
  return i;
}

static std::optional<std::string> parse_string(const std::string& s, size_t& i) {
  i = skip_ws(s, i);
  if (i >= s.size() || s[i] != '"') return std::nullopt;
  i++;
  std::string out;
  while (i < s.size()) {
    char c = s[i++];
    if (c == '"') return out;
    if (c == '\\') {
      if (i >= s.size()) return std::nullopt;
      char n = s[i++];
      if (n == 'n') out.push_back('\n');
      else if (n == 'r') out.push_back('\r');
      else if (n == 't') out.push_back('\t');
      else out.push_back(n);
      continue;
    }
    out.push_back(c);
  }
  return std::nullopt;
}

static bool parse_bool_or_false(const std::string& s, size_t& i) {
  i = skip_ws(s, i);
  if (s.compare(i, 4, "true") == 0) {
    i += 4;
    return true;
  }
  if (s.compare(i, 5, "false") == 0) {
    i += 5;
    return false;
  }
  return false;
}

static void expect(const std::string& s, size_t& i, char ch) {
  i = skip_ws(s, i);
  if (i >= s.size() || s[i] != ch) {
    throw cpp_agent::core::AgentError(cpp_agent::core::ErrorCode::kInvalidArgument,
                                     std::string("Invalid plan.json near '") + ch + "'");
  }
  i++;
}

static void skip_value(const std::string& s, size_t& i);

static void skip_object(const std::string& s, size_t& i) {
  expect(s, i, '{');
  for (;;) {
    i = skip_ws(s, i);
    if (i < s.size() && s[i] == '}') {
      i++;
      return;
    }
    auto k = parse_string(s, i);
    if (!k) throw cpp_agent::core::AgentError(cpp_agent::core::ErrorCode::kInvalidArgument, "Invalid JSON key");
    expect(s, i, ':');
    skip_value(s, i);
    i = skip_ws(s, i);
    if (i < s.size() && s[i] == ',') i++;
  }
}

static void skip_array(const std::string& s, size_t& i) {
  expect(s, i, '[');
  for (;;) {
    i = skip_ws(s, i);
    if (i < s.size() && s[i] == ']') {
      i++;
      return;
    }
    skip_value(s, i);
    i = skip_ws(s, i);
    if (i < s.size() && s[i] == ',') i++;
  }
}

static void skip_value(const std::string& s, size_t& i) {
  i = skip_ws(s, i);
  if (i >= s.size()) return;
  if (s[i] == '"') {
    auto tmp = parse_string(s, i);
    if (!tmp) return;
    return;
  }
  if (s[i] == '{') return skip_object(s, i);
  if (s[i] == '[') return skip_array(s, i);
  // bool/null/number
  while (i < s.size() && s[i] != ',' && s[i] != ']' && s[i] != '}') i++;
}

static std::optional<std::string> find_string_field(const std::string& s, const std::string& key, size_t start, size_t end) {
  auto pos = s.find('"' + key + '"', start);
  if (pos == std::string::npos || pos >= end) return std::nullopt;
  pos = s.find(':', pos);
  if (pos == std::string::npos || pos >= end) return std::nullopt;
  pos++;
  size_t i = pos;
  auto str = parse_string(s, i);
  return str;
}

static bool find_bool_field(const std::string& s, const std::string& key, size_t start, size_t end) {
  auto pos = s.find('"' + key + '"', start);
  if (pos == std::string::npos || pos >= end) return false;
  pos = s.find(':', pos);
  if (pos == std::string::npos || pos >= end) return false;
  pos++;
  size_t i = pos;
  return parse_bool_or_false(s, i);
}

static std::vector<std::string> parse_string_array_field(const std::string& s,
                                                         const std::string& key,
                                                         size_t start,
                                                         size_t end) {
  auto pos = s.find('"' + key + '"', start);
  if (pos == std::string::npos || pos >= end) return {};
  pos = s.find(':', pos);
  if (pos == std::string::npos || pos >= end) return {};
  pos++;
  size_t i = skip_ws(s, pos);
  if (i >= s.size() || s[i] != '[') return {};
  i++;
  std::vector<std::string> out;
  for (;;) {
    i = skip_ws(s, i);
    if (i >= s.size()) break;
    if (s[i] == ']') {
      i++;
      break;
    }
    auto str = parse_string(s, i);
    if (!str) break;
    out.push_back(*str);
    i = skip_ws(s, i);
    if (i < s.size() && s[i] == ',') i++;
  }
  return out;
}

static size_t find_field_value_start(const std::string& s, const std::string& key, size_t start, size_t end) {
  auto pos = s.find('"' + key + '"', start);
  if (pos == std::string::npos || pos >= end) return std::string::npos;
  pos = s.find(':', pos);
  if (pos == std::string::npos || pos >= end) return std::string::npos;
  pos++;
  return skip_ws(s, pos);
}

static size_t span_json_value(const std::string& s, size_t i) {
  i = skip_ws(s, i);
  if (i >= s.size()) return i;
  if (s[i] == '"') {
    size_t j = i;
    auto tmp = parse_string(s, j);
    (void)tmp;
    return j;
  }
  if (s[i] == '{') {
    size_t j = i;
    skip_object(s, j);
    return j;
  }
  if (s[i] == '[') {
    size_t j = i;
    skip_array(s, j);
    return j;
  }
  while (i < s.size() && s[i] != ',' && s[i] != ']' && s[i] != '}') i++;
  return i;
}

static Task parse_task_object(const std::string& s, size_t start, size_t end);

static std::vector<Task> parse_tasks_array(const std::string& s, size_t start, size_t end) {
  size_t i = skip_ws(s, start);
  if (i >= s.size() || s[i] != '[') return {};
  i++;
  std::vector<Task> out;
  for (;;) {
    i = skip_ws(s, i);
    if (i >= s.size()) break;
    if (s[i] == ']') {
      i++;
      break;
    }
    if (s[i] != '{') {
      // skip unknown
      skip_value(s, i);
    } else {
      size_t obj_start = i;
      size_t obj_end = span_json_value(s, i);
      out.push_back(parse_task_object(s, obj_start, obj_end));
      i = obj_end;
    }
    i = skip_ws(s, i);
    if (i < s.size() && s[i] == ',') i++;
  }
  return out;
}

static Task parse_task_object(const std::string& s, size_t start, size_t end) {
  Task t;
  auto goal = find_string_field(s, "goal", start, end);
  auto title = find_string_field(s, "title", start, end);
  if (goal) t.goal = *goal;
  if (title) t.title = *title;
  t.active = find_bool_field(s, "active", start, end);
  t.history = parse_string_array_field(s, "history", start, end);

  auto children_pos = find_field_value_start(s, "children", start, end);
  if (children_pos != std::string::npos) {
    size_t children_end = span_json_value(s, children_pos);
    t.children = parse_tasks_array(s, children_pos, children_end);
  }
  return t;
}

static Plan parse_plan_or_empty(const std::string& s) {
  Plan p;
  if (s.empty()) return p;

  auto vpos = s.find("\"version\"");
  if (vpos == std::string::npos) return p;

  auto tasks_pos = s.find("\"tasks\"");
  if (tasks_pos == std::string::npos) return p;
  auto arr_start = s.find('[', tasks_pos);
  if (arr_start == std::string::npos) return p;
  size_t arr_end = span_json_value(s, arr_start);
  p.tasks = parse_tasks_array(s, arr_start, arr_end);
  return p;
}

static void write_task_json(std::ostringstream& oss, const Task& t) {
  oss << "{";
  oss << "\"goal\":\"" << json_escape(t.goal) << "\",";
  oss << "\"title\":\"" << json_escape(t.title) << "\"";
  if (t.active) {
    oss << ",\"active\":true";
  }
  if (!t.history.empty()) {
    oss << ",\"history\":[";
    for (size_t i = 0; i < t.history.size(); ++i) {
      if (i) oss << ',';
      oss << "\"" << json_escape(t.history[i]) << "\"";
    }
    oss << "]";
  }
  if (!t.children.empty()) {
    oss << ",\"children\":[";
    for (size_t i = 0; i < t.children.size(); ++i) {
      if (i) oss << ',';
      write_task_json(oss, t.children[i]);
    }
    oss << "]";
  }
  oss << "}";
}

static std::string serialize_plan_json(const Plan& p) {
  std::ostringstream oss;
  oss << "{\"version\":" << p.version << ",\"tasks\":[";
  for (size_t i = 0; i < p.tasks.size(); ++i) {
    if (i) oss << ',';
    write_task_json(oss, p.tasks[i]);
  }
  oss << "]}";
  return oss.str();
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
  // completed display: recently_completed handled elsewhere
  oss << t.title;
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
  oss << "## Tasks\n";

  auto chain = compute_active_chain(plan_.tasks);
  for (size_t i = 0; i < plan_.tasks.size(); ++i) {
    render_task(oss, plan_.tasks[i], std::to_string(i + 1), false, chain, 0);
  }

  // Render recently completed markers in-place (best-effort) after normal tasks.
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
  std::lock_guard<std::mutex> lock(mu_);

  Task t;
  t.goal = goal;
  t.title = title;

  if (!parent_no) {
    int after_index = -1;
    if (after_no) {
      auto ref = find_by_no(plan_, *after_no);
      if (ref.task && !ref.parent) after_index = ref.index;
    }
    insert_task(plan_.tasks, std::move(t), after_index);
    persist_locked();
    return "ok";
  }

  auto parent_ref = find_by_no(plan_, *parent_no);
  if (!parent_ref.task) return "Parent not found";

  int after_index = -1;
  if (after_no) {
    auto ref = find_by_no(plan_, *after_no);
    if (ref.task && ref.parent == parent_ref.task) after_index = ref.index;
  }
  insert_task(parent_ref.task->children, std::move(t), after_index);
  persist_locked();
  return "ok";
}

std::string PlanStore::active(const TaskNo& no) {
  std::lock_guard<std::mutex> lock(mu_);
  auto ref = find_by_no(plan_, no);
  if (!ref.task) return "Task not found";
  if (!ref.task->children.empty()) return "Task is not a leaf";
  clear_active(plan_);
  ref.task->active = true;
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

  // Track current active path before deletion.
  Task* active_ptr = find_active_ptr(plan_.tasks);
  std::vector<int> active_path;
  if (active_ptr) {
    build_path_to_active(plan_.tasks, active_ptr, active_path);
  }

  auto ref = find_by_no(plan_, no);
  if (!ref.task) return "Task not found";

  bool was_active = ref.task->active;

  // Capture title and positional address for strike-through rendering.
  CompletedMarker m;
  m.no = no;
  m.title = ref.task->title;
  recently_completed_.push_back(std::move(m));

  if (!ref.parent) {
    plan_.tasks.erase(plan_.tasks.begin() + ref.index);
  } else {
    ref.parent->children.erase(ref.parent->children.begin() + ref.index);
  }

  if (was_active || active_ptr == ref.task) {
    clear_active(plan_);
  }

  // Recompute next active leaf near the previous active path.
  if (!active_path.empty()) {
    auto leaf = find_leaf_next_to_path(plan_.tasks, active_path);
    if (leaf.task) {
      clear_active(plan_);
      leaf.task->active = true;
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
  // Ensure at most one active; keep first leaf active if multiple.
  Task* found = nullptr;
  std::vector<Task*> stack;
  for (auto& t : plan_.tasks) stack.push_back(&t);
  while (!stack.empty()) {
    Task* t = stack.back();
    stack.pop_back();
    if (t->active) {
      if (!found) {
        if (t->children.empty()) {
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

  if (!found) return;
}

void PlanStore::persist_locked() {
  std::error_code ec;
  std::filesystem::create_directories(path_.parent_path(), ec);
  std::ofstream ofs(path_);
  if (!ofs) {
    throw cpp_agent::core::AgentError(cpp_agent::core::ErrorCode::kIo,
                                     "Failed to write plan.json: " + path_.string());
  }
  ofs << serialize_plan_json(plan_);
}

} // namespace cpp_agent::infra::plan
