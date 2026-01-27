#pragma once

#include <string>

namespace agent {

struct Skill {
  std::string name;
  std::string description;
  std::string prompt_md;

  // For diagnostics.
  std::string json_path;
  std::string md_path;
};

} // namespace agent
