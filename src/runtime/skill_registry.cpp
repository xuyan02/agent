#include "runtime/skill_registry.h"

#include "llm/file_io.h"
#include "json/json.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <iostream>
#include <system_error>

namespace agent {

static std::string PathToString(const std::filesystem::path& p) {
  // Keep it simple; callers only use this for diagnostics.
  return p.string();
}

bool SkillRegistry::LoadFromDir(const std::filesystem::path& skills_dir) {
  by_name_.clear();

  std::error_code ec;
  if (!std::filesystem::exists(skills_dir, ec)) {
    std::cerr << "warning: skills dir not found: " << skills_dir.string() << "\n";
    return true;
  }

  if (!std::filesystem::is_directory(skills_dir, ec)) {
    std::cerr << "error: skills path is not a directory: " << skills_dir.string() << "\n";
    return false;
  }

  for (const auto& ent : std::filesystem::directory_iterator(skills_dir, ec)) {
    if (ec) break;
    if (!ent.is_regular_file()) continue;
    if (ent.path().extension() != ".json") continue;

    std::string json;
    if (!agent::ReadAll(ent.path(), &json)) {
      std::cerr << "error: failed to read skill json: " << ent.path().string() << "\n";
      continue;
    }

    auto root_opt = agent::json::Parse(json);
    if (!root_opt) {
      std::cerr << "error: failed to parse skill json: " << ent.path().string() << "\n";
      continue;
    }
    const auto& root = *root_opt;

    auto name_opt = agent::json::GetString(root, "name");
    auto desc_opt = agent::json::GetString(root, "description");
    auto prompt_md_rel_opt = agent::json::GetString(root, "prompt_md");

    if (!name_opt || name_opt->empty()) {
      std::cerr << "error: skill json missing name: " << ent.path().string() << "\n";
      continue;
    }
    if (!desc_opt || desc_opt->empty()) {
      std::cerr << "error: skill json missing description: " << ent.path().string() << "\n";
      continue;
    }
    if (!prompt_md_rel_opt || prompt_md_rel_opt->empty()) {
      std::cerr << "error: skill json missing prompt_md: " << ent.path().string() << "\n";
      continue;
    }

    std::vector<std::string> tools;
    // tools is optional.
    // - missing: ok (empty)
    // - present but invalid: error
    if (root.is_object() && root.contains("tools")) {
      auto tools_opt = agent::json::GetStringArray(root, "tools");
      if (!tools_opt.has_value()) {
        std::cerr << "error: skill json invalid tools (expect string array): " << ent.path().string() << "\n";
        continue;
      }
      tools = std::move(*tools_opt);
    }

    std::string name = std::move(*name_opt);
    std::string description = std::move(*desc_opt);
    std::string prompt_md_rel = std::move(*prompt_md_rel_opt);

    if (by_name_.find(name) != by_name_.end()) {
      std::cerr << "error: duplicate skill name: " << name << " ("
                << ent.path().string() << ")\n";
      continue;
    }

    const auto md_path = skills_dir / prompt_md_rel;
    std::string prompt_md;
    if (!agent::ReadAll(md_path, &prompt_md)) {
      std::cerr << "error: failed to read skill prompt_md: " << md_path.string() << "\n";
      continue;
    }

    Skill s;
    s.name = std::move(name);
    s.description = std::move(description);
    s.prompt_md = std::move(prompt_md);
    s.tools = std::move(tools);
    s.json_path = PathToString(ent.path());
    s.md_path = PathToString(md_path);

    by_name_.insert({s.name, std::move(s)});
  }

  if (ec) {
    std::cerr << "error: failed to scan skills dir: " << skills_dir.string()
              << " (" << ec.message() << ")\n";
    return false;
  }

  return true;
}

const Skill* SkillRegistry::Find(const std::string& name) const {
  auto it = by_name_.find(name);
  if (it == by_name_.end()) return nullptr;
  return &it->second;
}

std::vector<std::string> SkillRegistry::ListNamesSorted() const {
  std::vector<std::string> out;
  out.reserve(by_name_.size());
  for (const auto& kv : by_name_) out.push_back(kv.first);
  std::sort(out.begin(), out.end());
  return out;
}

} // namespace agent
