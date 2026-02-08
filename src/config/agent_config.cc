#include "config/agent_config.h"

#include <cctype>
#include <fstream>
#include <sstream>
#include <string>

namespace agent {
namespace {

static inline std::string Trim(std::string s) {
  size_t b = 0;
  while (b < s.size() && std::isspace(static_cast<unsigned char>(s[b])))
    b++;
  size_t e = s.size();
  while (e > b && std::isspace(static_cast<unsigned char>(s[e - 1])))
    e--;
  s.erase(e);
  s.erase(0, b);
  return s;
}

static inline bool StartsWith(const std::string& s, const char* p) {
  return s.rfind(p, 0) == 0;
}

static inline std::string StripQuotes(std::string v) {
  if (v.size() >= 2) {
    const char a = v.front();
    const char b = v.back();
    if ((a == '"' && b == '"') || (a == '\'' && b == '\''))
      return v.substr(1, v.size() - 2);
  }
  return v;
}

}  // namespace

std::optional<AgentConfig> LoadAgentConfigYaml(const std::string& path,
                                               std::string* error_out) {
  std::ifstream in(path);
  if (!in) {
    if (error_out)
      *error_out = "failed to open: " + path;
    return std::nullopt;
  }

  AgentConfig cfg;

  bool in_openai = false;
  int openai_indent = -1;

  std::string line;
  int line_no = 0;
  while (std::getline(in, line)) {
    line_no++;

    const size_t hash = line.find('#');
    if (hash != std::string::npos)
      line.erase(hash);

    if (Trim(line).empty())
      continue;

    int indent = 0;
    while (indent < static_cast<int>(line.size()) && line[indent] == ' ')
      indent++;

    std::string t = Trim(line);
    if (t.empty())
      continue;

    // Section handling.
    if (StartsWith(t, "openai:")) {
      in_openai = true;
      openai_indent = indent;
      if (!cfg.openai)
        cfg.openai = OpenAiProviderConfig{};
      continue;
    }

    if (in_openai && indent <= openai_indent) {
      in_openai = false;
      openai_indent = -1;
    }

    const size_t colon = t.find(':');
    if (colon == std::string::npos)
      continue;

    std::string key = Trim(t.substr(0, colon));
    std::string value = Trim(t.substr(colon + 1));
    value = StripQuotes(value);

    if (!in_openai) {
      if (key == "model") {
        cfg.model = value;
      }
      continue;
    }

    // openai subsection
    if (key == "base_url") {
      cfg.openai->base_url = value;
    } else if (key == "api_key") {
      cfg.openai->api_key = value;
    }
  }

  if (cfg.model.empty()) {
    if (error_out)
      *error_out = "missing required key: model";
    return std::nullopt;
  }

  if (cfg.openai) {
    if (cfg.openai->base_url.empty())
      cfg.openai->base_url = "https://api.openai.com";
    if (cfg.openai->api_key.empty()) {
      if (error_out)
        *error_out = "openai.api_key is required when openai provider is configured";
      return std::nullopt;
    }
  }

  return cfg;
}

}  // namespace agent
