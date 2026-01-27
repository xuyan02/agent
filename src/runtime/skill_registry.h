#pragma once

#include "runtime/skill.h"

#include <filesystem>
#include <string>
#include <unordered_map>
#include <vector>

namespace agent {

class SkillRegistry {
public:
  SkillRegistry() = default;

  bool LoadFromDir(const std::filesystem::path& skills_dir);

  const Skill* Find(const std::string& name) const;
  std::vector<std::string> ListNamesSorted() const;

private:
  std::unordered_map<std::string, Skill> by_name_;
};

} // namespace agent
