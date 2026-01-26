#pragma once

#include "runtime/general_agent.h"

#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace agent {

class Runtime;

class Team {
public:
  static std::unique_ptr<Team> Load(Runtime& runtime,
                                    agent::LlmContext& llm,
                                    const std::string& path,
                                    const std::string& default_model);

  bool Save(const std::string& path) const;

  explicit Team(std::string leader);

  bool Add(std::unique_ptr<GeneralAgent> agent);
  GeneralAgent* Find(const std::string& name);

  std::string leader() const;

private:
  std::string leader_;
  std::unordered_map<std::string, std::unique_ptr<GeneralAgent>> agents_;
  std::vector<std::string> agent_names_in_order_;
};

} // namespace agent
