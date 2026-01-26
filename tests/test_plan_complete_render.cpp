#include "infra/plan/plan_store.h"

#include <cassert>
#include <filesystem>
#include <memory>

int main() {
  auto tmp = std::filesystem::temp_directory_path() / "cpp-agent-plan-test-complete.json";
  std::error_code ec;
  std::filesystem::remove(tmp, ec);

  auto store = std::make_shared<agent::PlanStore>(tmp);
  store->load();

  store->add(std::nullopt, "g1", "t1", std::nullopt);
  store->add(std::nullopt, "g2", "t2", std::nullopt);

  // Complete first root task.
  store->complete(*agent::parse_task_no("1"));

  auto md = store->render_markdown();
  // Should render completion marker with previous no.
  assert(md.find("1. ~~t1~~") != std::string::npos);

  std::filesystem::remove(tmp, ec);
  return 0;
}
