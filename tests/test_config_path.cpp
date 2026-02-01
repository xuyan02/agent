#include "app/config.h"

#include <cassert>
#include <filesystem>
#include <fstream>
#include <string>

static void write_text(const std::filesystem::path& p, const std::string& s) {
  std::ofstream ofs(p);
  assert(ofs);
  ofs << s;
}

int main() {
  namespace fs = std::filesystem;

  const fs::path tmp = fs::temp_directory_path() / "cpp-agent-test-config-path";
  fs::remove_all(tmp);
  fs::create_directories(tmp);

  const fs::path cfg_path = tmp / "settings.json";

  // Minimal config required by load_config_or_throw().
  write_text(cfg_path,
             "{\n"
             "  \"providers_json_path\": \"/tmp/cpp-agent-llm.json\",\n"
             "  \"model\": \"test-model\",\n"
             "  \"project_root\": \".\",\n"
             "  \"storage_dir\": \".\"\n"
             "}\n");

  const fs::path old = fs::current_path();
  fs::current_path(tmp);

  auto cfg_r = agent::load_config(cfg_path);
  assert(cfg_r);
  assert(cfg_r->llm.providers_json_path == "/tmp/cpp-agent-llm.json");
  assert(cfg_r->llm.model == "test-model");

  // Verify "~" expansion for config path.
  const char* home = std::getenv("HOME");
  if (home && *home) {
    const fs::path home_tmp = fs::path(home) / "cpp-agent-test-config-path";
    fs::remove_all(home_tmp);
    fs::create_directories(home_tmp);

    const fs::path home_cfg = home_tmp / "settings.json";
    write_text(home_cfg,
               "{\n"
               "  \"providers_json_path\": \"~/llm.json\",\n"
               "  \"model\": \"test-model\",\n"
               "  \"project_root\": \"~/proj\",\n"
               "  \"storage_dir\": \"~/store\"\n"
               "}\n");

    auto cfg2_r = agent::load_config("~/cpp-agent-test-config-path/settings.json");
    assert(cfg2_r);
    assert(cfg2_r->llm.providers_json_path == fs::path(home) / "llm.json");
    assert(cfg2_r->project_root == fs::path(home) / "proj");
    assert(cfg2_r->storage_dir == fs::path(home) / "store");

    fs::remove_all(home_tmp);
  }

  fs::current_path(old);
  fs::remove_all(tmp);

  return 0;
}
