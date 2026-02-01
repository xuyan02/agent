#pragma once

#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>

namespace agent {

inline bool ReadAll(const std::filesystem::path& path, std::string* out) {
  if (!out) return false;
  std::ifstream f(path);
  if (!f) return false;
  std::ostringstream ss;
  ss << f.rdbuf();
  *out = ss.str();
  return true;
}

} // namespace agent
