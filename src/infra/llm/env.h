#pragma once

#include <cstdlib>
#include <string>

namespace cpp_agent::infra::llm {

inline std::string ResolveEnvValue(std::string v) {
  auto trim = [](const std::string& x) {
    size_t a = 0;
    while (a < x.size() && (x[a] == ' ' || x[a] == '\n' || x[a] == '\t' || x[a] == '\r')) a++;
    size_t b = x.size();
    while (b > a && (x[b - 1] == ' ' || x[b - 1] == '\n' || x[b - 1] == '\t' || x[b - 1] == '\r')) b--;
    return x.substr(a, b - a);
  };

  v = trim(v);
  constexpr const char* kPrefix = "ENV:";
  if (v.rfind(kPrefix, 0) == 0) {
    auto env = v.substr(std::string(kPrefix).size());
    const char* val = std::getenv(env.c_str());
    return val ? std::string(val) : std::string();
  }
  return v;
}

} // namespace cpp_agent::infra::llm
