#include "runtime/team.h"

#include "runtime/runtime.h"

#include <fstream>
#include <iostream>
#include <sstream>

namespace agent {

static std::string ReadAll(const std::string& path) {
  std::ifstream ifs(path);
  if (!ifs) return "";
  std::ostringstream oss;
  oss << ifs.rdbuf();
  return oss.str();
}

// Extremely small JSON extractor for {"leader":"x","agents":[{"name":"a"},...]}
// Prototype-only (no dependency on JSON libs).
static std::string ExtractString(const std::string& json, const std::string& key) {
  auto pos = json.find('"' + key + '"');
  if (pos == std::string::npos) return "";
  pos = json.find(':', pos);
  if (pos == std::string::npos) return "";
  pos = json.find('"', pos);
  if (pos == std::string::npos) return "";
  pos++;
  auto end = json.find('"', pos);
  if (end == std::string::npos) return "";
  return json.substr(pos, end - pos);
}

static std::vector<std::string> ExtractAgentNames(const std::string& json) {
  std::vector<std::string> out;
  auto agents_pos = json.find("\"agents\"");
  if (agents_pos == std::string::npos) return out;
  auto pos = json.find('[', agents_pos);
  if (pos == std::string::npos) return out;
  pos++;
  while (pos < json.size()) {
    auto name_pos = json.find("\"name\"", pos);
    if (name_pos == std::string::npos) break;
    auto n = ExtractString(json.substr(name_pos), "name");
    if (!n.empty()) out.push_back(n);
    pos = name_pos + 6;
  }
  return out;
}

std::unique_ptr<Team> Team::Load(Runtime& runtime,
                                 agent::LlmContext& llm,
                                 const std::string& path,
                                 const std::string& default_model) {
  const std::string json = ReadAll(path);
  if (json.empty()) {
    std::cerr << "error: failed to read team.json: " << path << "\n";
    return nullptr;
  }

  const std::string leader = ExtractString(json, "leader");
  if (leader.empty()) {
    std::cerr << "error: team.json missing leader\n";
    return nullptr;
  }

  auto names = ExtractAgentNames(json);
  if (names.empty()) {
    std::cerr << "error: team.json has no agents\n";
    return nullptr;
  }

  auto team = std::make_unique<Team>(leader);

  bool leader_found = false;
  std::unordered_map<std::string, bool> seen;
  for (const auto& n : names) {
    if (n.empty()) continue;
    if (seen[n]) {
      std::cerr << "error: duplicate agent name in team.json: " << n << "\n";
      return nullptr;
    }
    seen[n] = true;
    if (n == leader) leader_found = true;

    auto agent = std::make_unique<GeneralAgent>(runtime, n, llm, default_model);
    if (!team->Add(std::move(agent))) {
      std::cerr << "error: failed to add agent: " << n << "\n";
      return nullptr;
    }
  }

  if (!leader_found) {
    std::cerr << "error: leader not found in agents list: " << leader << "\n";
    return nullptr;
  }

  return team;
}

bool Team::Save(const std::string& path) const {
  std::ofstream ofs(path);
  if (!ofs) return false;

  ofs << "{\n";
  ofs << "  \"leader\": \"" << leader_ << "\",\n";
  ofs << "  \"agents\": [\n";
  for (size_t i = 0; i < agent_names_in_order_.size(); ++i) {
    ofs << "    {\"name\": \"" << agent_names_in_order_[i] << "\"}";
    if (i + 1 < agent_names_in_order_.size()) ofs << ",";
    ofs << "\n";
  }
  ofs << "  ]\n";
  ofs << "}\n";
  return true;
}

Team::Team(std::string leader) : leader_(std::move(leader)) {}

bool Team::Add(std::unique_ptr<GeneralAgent> agent) {
  if (!agent) return false;
  const auto n = agent->name();
  if (n.empty()) return false;
  if (agents_.find(n) != agents_.end()) return false;
  agent_names_in_order_.push_back(n);
  agents_.emplace(n, std::move(agent));
  return true;
}

GeneralAgent* Team::Find(const std::string& name) {
  auto it = agents_.find(name);
  if (it == agents_.end()) return nullptr;
  return it->second.get();
}

std::string Team::leader() const { return leader_; }

} // namespace agent
