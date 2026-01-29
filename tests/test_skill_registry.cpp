#include "runtime/skill_registry.h"

#include <cassert>
#include <filesystem>
#include <fstream>
#include <string>

namespace {

static void write_file(const std::filesystem::path& p, const std::string& content) {
  std::filesystem::create_directories(p.parent_path());
  std::ofstream f(p);
  f << content;
}

static std::filesystem::path make_temp_dir() {
  auto base = std::filesystem::temp_directory_path();
  auto dir = base / "cpp-agent-skill-registry";
  std::error_code ec;
  std::filesystem::remove_all(dir, ec);
  std::filesystem::create_directories(dir);
  return dir;
}

} // namespace

int main() {
  const auto dir = make_temp_dir();

  // Valid skill.
  // tools is optional.
  write_file(dir / "a.json", R"JSON({
    "name": "a",
    "description": "desc",
    "prompt_md": "a.md"
  })JSON");
  write_file(dir / "a.md", "hello\n");

  // Invalid skill: missing md.
  write_file(dir / "b.json", R"JSON({
    "name": "b",
    "description": "desc",
    "prompt_md": "missing.md"
  })JSON");

  // Invalid skill: tools present but wrong type.
  write_file(dir / "c.json", R"JSON({
    "name": "c",
    "description": "desc",
    "prompt_md": "a.md",
    "tools": "not_array"
  })JSON");

  // Invalid skill: tools array contains non-string.
  write_file(dir / "d.json", R"JSON({
    "name": "d",
    "description": "desc",
    "prompt_md": "a.md",
    "tools": ["ok", 1]
  })JSON");

  agent::SkillRegistry reg;
  const bool ok = reg.LoadFromDir(dir);
  assert(ok);

  {
    const auto* a = reg.Find("a");
    assert(a);
    assert(a->name == "a");
    assert(a->description == "desc");
    assert(a->prompt_md == "hello\n");
  }

  {
    const auto* b = reg.Find("b");
    assert(!b);
  }

  {
    const auto* c = reg.Find("c");
    assert(!c);
  }

  {
    const auto* d = reg.Find("d");
    assert(!d);
  }

  return 0;
}
