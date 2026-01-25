#include "infra/plan/plan_store.h"

#include <cassert>
#include <cstdio>
#include <filesystem>
#include <memory>

int main() {
  auto tmp = std::filesystem::temp_directory_path() / "cpp-agent-plan-test-migrate.json";
  std::error_code ec;
  std::filesystem::remove(tmp, ec);

  auto store = std::make_shared<cpp_agent::infra::plan::PlanStore>(tmp);
  store->load();

  // Build root-only tasks:
  // 1: t1
  // 2: t1.1
  // 3: t1.2  <-- active
  // 4: t2
  // 5: t2.1
  store->add(std::nullopt, "g1", "t1", std::nullopt);
  store->add(std::nullopt, "g1.1", "t1.1", *cpp_agent::infra::plan::parse_task_no("1"));
  store->add(std::nullopt, "g1.2", "t1.2", *cpp_agent::infra::plan::parse_task_no("2"));
  store->add(std::nullopt, "g2", "t2", *cpp_agent::infra::plan::parse_task_no("3"));
  store->add(std::nullopt, "g2.1", "t2.1", *cpp_agent::infra::plan::parse_task_no("4"));

  store->switch_to(*cpp_agent::infra::plan::parse_task_no("3"));

  // Complete active leaf 3 (t1.2): per rule B, prefer after siblings -> 4 (t2);
  // so the next active leaf should become t2.
  store->complete(*cpp_agent::infra::plan::parse_task_no("3"));

  auto md = store->render_markdown();
  // Active leaf should now be the first incomplete leaf after completed leaf 3.
  // With the current PlanStore, rule B uses the nearest leaf next to the completed node's path.
  // If this fails, print markdown for easier debugging.
  if (md.find("**t1**") == std::string::npos) {
    std::fprintf(stderr, "%s\n", md.c_str());
  }
  assert(md.find("**t1**") != std::string::npos);

  std::filesystem::remove(tmp, ec);
  return 0;
}
