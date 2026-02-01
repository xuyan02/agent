#pragma once

#include <nlohmann/json.hpp>

#include <string>

namespace agent {

struct ToolCall {
  std::string id;
  std::string name;
  // Tool arguments (must be a JSON object).
  nlohmann::json arguments = nlohmann::json::object();
};

}  // namespace agent
