#pragma once

#include "runtime/general_agent.h"
#include "runtime/tool.h"

#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace agent {

class Runtime;

class Team {
public:
  static std::unique_ptr<Team> Load(Runtime& runtime,
                                    const std::string& path,
                                    const std::string& default_model);

  bool Save(const std::string& path) const;

  explicit Team(Runtime& runtime, std::string leader);

  Runtime& runtime();
  const Runtime& runtime() const;

  bool Add(std::unique_ptr<GeneralAgent> agent);
  GeneralAgent* Find(const std::string& name);

  std::string leader() const;

  std::vector<Tool> GetTools() const;

private:
  Runtime& runtime_;
  std::string leader_;
  std::unordered_map<std::string, std::unique_ptr<GeneralAgent>> agents_;
  std::vector<std::string> agent_names_in_order_;
};

} // namespace agent
