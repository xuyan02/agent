#include "runtime/skill_registry.h"

#include <cassert>
#include <filesystem>
#include <string>

int main() {
  agent::SkillRegistry r;
  r.LoadFromDir(std::filesystem::current_path() / "skills");

  const auto* plan = r.Find("plan");
  assert(plan);
  assert(!plan->tools.empty());
  assert(plan->tools[0] == "plan");

  const auto* ga = r.Find("general_agent");
  assert(ga);

  return 0;
}
