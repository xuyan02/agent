#include "tool/file_tool.h"

#include "agent/agent_context.h"
#include "agent/session.h"

#include "dust/memory/ref_ptr.h"

#include <cassert>
#include <filesystem>
#include <fstream>

#include <nlohmann/json.hpp>

static void WriteText(const std::filesystem::path& p, const std::string& s) {
  std::error_code ec;
  std::filesystem::create_directories(p.parent_path(), ec);
  std::ofstream f(p, std::ios::out | std::ios::binary | std::ios::trunc);
  assert(f.is_open());
  f.write(s.data(), static_cast<std::streamsize>(s.size()));
}

int main() {
  const auto workspace = std::filesystem::temp_directory_path() / "cpp_agent_file_tool";
  std::error_code ec;
  std::filesystem::remove_all(workspace, ec);
  std::filesystem::create_directories(workspace, ec);

  WriteText(workspace / "src/a.txt", "hello world\n");
  WriteText(workspace / "build/ignored.txt", "ignore\n");

  auto session = std::move(agent::Session::Builder()
                               .SetWorkspacePath(workspace)
                               .SetDefaultModel("fake"))
                     .Build();
  auto ctx = std::move(agent::AgentContext::Builder().SetSession(session)).Build();
  auto tool = dust::MakeRefPtr<agent::FileTool>();

  // read
  {
    nlohmann::json args;
    args["path"] = "src/a.txt";
    auto fut = tool->Invoke(ctx, "file.read", args);
    assert(fut);
    dust::PollContext pc{dust::WakerHandle()};
    auto polled = fut->PollOnce(pc);
    assert(polled.is_ready());
    auto out = polled.TakeReady();
    assert(out["ok"] == true);
    assert(out["data"]["content"].is_string());
  }

  // read rejects absolute
  {
    nlohmann::json args;
    args["path"] = (workspace / "src/a.txt").string();
    auto fut = tool->Invoke(ctx, "file.read", args);
    assert(fut);
    dust::PollContext pc{dust::WakerHandle()};
    auto out = fut->PollOnce(pc).TakeReady();
    assert(out["ok"] == false);
  }

  // read rejects ..
  {
    nlohmann::json args;
    args["path"] = "../src/a.txt";
    auto fut = tool->Invoke(ctx, "file.read", args);
    assert(fut);
    dust::PollContext pc{dust::WakerHandle()};
    auto out = fut->PollOnce(pc).TakeReady();
    assert(out["ok"] == false);
  }

  // glob: exact match and ignore build/
  {
    nlohmann::json args;
    args["pattern"] = "src/a.txt";
    auto fut = tool->Invoke(ctx, "file.glob", args);
    assert(fut);
    dust::PollContext pc{dust::WakerHandle()};
    auto out = fut->PollOnce(pc).TakeReady();
    assert(out["ok"] == true);
    const auto& paths = out["data"]["paths"];
    bool saw_src = false;
    for (const auto& p : paths) {
      const std::string s = p.get<std::string>();
      if (s == "src/a.txt")
        saw_src = true;
    }
    assert(saw_src);

    nlohmann::json args2;
    args2["pattern"] = "build/ignored.txt";
    auto fut2 = tool->Invoke(ctx, "file.glob", args2);
    auto out2 = fut2->PollOnce(pc).TakeReady();
    assert(out2["ok"] == true);
    assert(out2["data"]["paths"].empty());
  }

  // grep
  {
    nlohmann::json args;
    args["pattern"] = "hello";
    args["glob"] = "**/a.txt";
    auto fut = tool->Invoke(ctx, "file.grep", args);
    assert(fut);
    dust::PollContext pc{dust::WakerHandle()};
    auto out = fut->PollOnce(pc).TakeReady();
    assert(out["ok"] == true);
    const auto& matches = out["data"]["matches"];
    assert(matches.is_array());
    bool found = false;
    for (const auto& m : matches) {
      if (m["path"].get<std::string>() == "src/a.txt")
        found = true;
    }
    assert(found);
  }

  // edit
  {
    nlohmann::json args;
    args["path"] = "src/a.txt";
    args["old"] = "world";
    args["new"] = "there";
    auto fut = tool->Invoke(ctx, "file.edit", args);
    assert(fut);
    dust::PollContext pc{dust::WakerHandle()};
    auto out = fut->PollOnce(pc).TakeReady();
    assert(out["ok"] == true);

    // verify change
    nlohmann::json r;
    r["path"] = "src/a.txt";
    auto rf = tool->Invoke(ctx, "file.read", r);
    auto out2 = rf->PollOnce(pc).TakeReady();
    assert(out2["ok"] == true);
    assert(out2["data"]["content"].get<std::string>().find("there") != std::string::npos);
  }

  return 0;
}
