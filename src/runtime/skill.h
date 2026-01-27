#pragma once

#include <string>
#include <vector>

namespace agent {

struct Skill {
  std::string name;
  std::string description;
  std::string prompt_md;

  // Tools that should be activated when this skill is active.
  std::vector<std::string> tools;

  // For diagnostics.
  std::string json_path;
  std::string md_path;
};

} // namespace agent
