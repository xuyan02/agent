#include "infra/plan/plan_store.h"

#include <cassert>
#include <filesystem>
#include <memory>

int main() {
  auto tmp = std::filesystem::temp_directory_path() / "cpp-agent-plan-test-migrate.json";
  std::error_code ec;
  std::filesystem::remove(tmp, ec);

  auto store = std::make_shared<cpp_agent::infra::plan::PlanStore>(tmp);
  store->load();

  // Build:
  // 1
  //   1.1 (leaf)
  //   1.2 (leaf)   <-- active
  // 2
  //   2.1 (leaf)
  store->add(std::nullopt, "g1", "t1", std::nullopt);
  store->add(*cpp_agent::infra::plan::parse_task_no("1"), "g1.1", "t1.1", std::nullopt);
  store->add(*cpp_agent::infra::plan::parse_task_no("1"), "g1.2", "t1.2", std::nullopt);

  store->add(std::nullopt, "g2", "t2", std::nullopt);
  store->add(*cpp_agent::infra::plan::parse_task_no("2"), "g2.1", "t2.1", std::nullopt);

  store->active(*cpp_agent::infra::plan::parse_task_no("1.2"));

  // Delete active leaf 1.2: per rule B, prefer after siblings under same parent -> none;
  // then before siblings -> 1.1 leaf.
  store->complete(*cpp_agent::infra::plan::parse_task_no("1.2"));

  auto md = store->render_markdown();
  // Active leaf should now be t1.1 (bold).
  assert(md.find("**t1.1**") != std::string::npos);

  std::filesystem::remove(tmp, ec);
  return 0;
}
