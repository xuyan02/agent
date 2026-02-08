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

  // glob: exact match, **/*, and ignore build/
  {
    dust::PollContext pc{dust::WakerHandle()};

    // exact match
    nlohmann::json args;
    args["pattern"] = "src/a.txt";
    auto fut = tool->Invoke(ctx, "file.glob", args);
    assert(fut);
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

    // match-all
    nlohmann::json all;
    all["pattern"] = "**/*";
    auto fut_all = tool->Invoke(ctx, "file.glob", all);
    assert(fut_all);
    auto out_all = fut_all->PollOnce(pc).TakeReady();
    assert(out_all["ok"] == true);
    bool saw_src_all = false;
    bool saw_build_all = false;
    for (const auto& p : out_all["data"]["paths"]) {
      const std::string s = p.get<std::string>();
      if (s == "src/a.txt")
        saw_src_all = true;
      if (s == "build/ignored.txt")
        saw_build_all = true;
    }
    assert(saw_src_all);
    assert(!saw_build_all);

    // ignored file should not match
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

  // write
  {
    dust::PollContext pc{dust::WakerHandle()};

    nlohmann::json w;
    w["path"] = "src/new.txt";
    w["content"] = "hello";
    auto fut = tool->Invoke(ctx, "file.write", w);
    assert(fut);
    auto out = fut->PollOnce(pc).TakeReady();
    assert(out["ok"] == true);

    // default overwrite=false should fail
    nlohmann::json w2;
    w2["path"] = "src/new.txt";
    w2["content"] = "world";
    auto fut2 = tool->Invoke(ctx, "file.write", w2);
    auto out2 = fut2->PollOnce(pc).TakeReady();
    assert(out2["ok"] == false);

    // overwrite=true should succeed
    nlohmann::json w3;
    w3["path"] = "src/new.txt";
    w3["content"] = "world";
    w3["overwrite"] = true;
    auto fut3 = tool->Invoke(ctx, "file.write", w3);
    auto out3 = fut3->PollOnce(pc).TakeReady();
    assert(out3["ok"] == true);

    // verify overwrite via read
    nlohmann::json rnew;
    rnew["path"] = "src/new.txt";
    auto rfnew = tool->Invoke(ctx, "file.read", rnew);
    auto out_new = rfnew->PollOnce(pc).TakeReady();
    assert(out_new["ok"] == true);
    assert(out_new["data"]["content"].get<std::string>().find("world") != std::string::npos);

    // ignored path should fail
    nlohmann::json w4;
    w4["path"] = "build/nope.txt";
    w4["content"] = "x";
    auto fut4 = tool->Invoke(ctx, "file.write", w4);
    auto out4 = fut4->PollOnce(pc).TakeReady();
    assert(out4["ok"] == false);

    // traversal should fail
    nlohmann::json w5;
    w5["path"] = "../escape.txt";
    w5["content"] = "x";
    auto fut5 = tool->Invoke(ctx, "file.write", w5);
    auto out5 = fut5->PollOnce(pc).TakeReady();
    assert(out5["ok"] == false);
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
