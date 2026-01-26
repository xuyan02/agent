#include "infra/plan/plan_store.h"
#include "infra/tools/plan_toolset.h"

#include <cassert>
#include <filesystem>
#include <memory>


int main() {
  auto tmp = std::filesystem::temp_directory_path() / "cpp-agent-plan-test.json";
  std::error_code ec;
  std::filesystem::remove(tmp, ec);

  auto store = std::make_shared<agent::PlanStore>(tmp);
  store->load();

  // Seed plan with one root task and one child, make child active.
  store->add(std::nullopt, "g1", "t1", std::nullopt);
  store->add(agent::parse_task_no("1"), "g1.1", "t1.1", std::nullopt);
  store->switch_to(*agent::parse_task_no("1.1"));

  agent::PlanReplanTool tool(store);

  agent::Policy policy(std::filesystem::current_path());

  auto res = tool.Invoke(
      "call1",
      "{\"no\":\"1\",\"history_line\":\"replan because x\",\"new_children\":[{\"goal\":\"ng\",\"title\":\"nt\",\"children\":[{\"goal\":\"ngc\",\"title\":\"ntc\"}]}]}",
      agent::ToolContext{policy});

  assert(res.ok);

  auto md = store->render_markdown();
  // Should include new child title.
  assert(md.find("nt") != std::string::npos);

  // Persisted file should exist.
  assert(std::filesystem::exists(tmp));

  std::filesystem::remove(tmp, ec);
  return 0;
}
