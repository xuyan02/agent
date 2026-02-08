#include "tool/file_tool.h"

#include "agent/agent_context.h"
#include "agent/session.h"
#include "json/json.h"
#include "tool/tool_spec.h"

#include "dust/async/just.h"

#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace agent {

namespace {

constexpr const char* kReadName = "file.read";
constexpr const char* kGlobName = "file.glob";
constexpr const char* kGrepName = "file.grep";
constexpr const char* kEditName = "file.edit";
constexpr const char* kWriteName = "file.write";

constexpr size_t kMaxReadBytes = 256 * 1024;
constexpr size_t kMaxWriteBytes = 256 * 1024;
constexpr size_t kMaxGrepMatches = 200;
constexpr size_t kMaxGlobMatches = 2000;

nlohmann::json Ok(nlohmann::json data) {
  nlohmann::json out;
  out["ok"] = true;
  out["data"] = std::move(data);
  return out;
}

nlohmann::json Err(std::string message) {
  nlohmann::json out;
  out["ok"] = false;
  out["error"] = std::move(message);
  return out;
}

bool StartsWithPath(const std::filesystem::path& base, const std::filesystem::path& p) {
  auto bit = base.begin();
  auto pit = p.begin();
  for (; bit != base.end(); ++bit, ++pit) {
    if (pit == p.end())
      return false;
    if (*bit != *pit)
      return false;
  }
  return true;
}

std::optional<std::filesystem::path> ResolveWorkspaceRelative(
    const std::filesystem::path& workspace,
    const nlohmann::json& args,
    const char* key,
    std::string* err) {
  auto rel = json::GetString(args, key);
  if (!rel) {
    if (err)
      *err = std::string("missing '") + key + "'";
    return std::nullopt;
  }

  std::filesystem::path rel_path(*rel);
  if (rel_path.is_absolute()) {
    if (err)
      *err = std::string("'") + key + "' must be relative";
    return std::nullopt;
  }

  // Reject any attempt to traverse upward.
  for (const auto& part : rel_path) {
    if (part == "..") {
      if (err)
        *err = std::string("'") + key + "' must not contain '..'";
      return std::nullopt;
    }
  }

  std::error_code ec;
  std::filesystem::path joined = workspace / rel_path;
  std::filesystem::path canon_workspace = std::filesystem::weakly_canonical(workspace, ec);
  if (ec)
    canon_workspace = workspace;
  std::filesystem::path canon_joined = std::filesystem::weakly_canonical(joined, ec);
  if (ec)
    canon_joined = joined;

  if (!StartsWithPath(canon_workspace, canon_joined)) {
    if (err)
      *err = "path escapes workspace";
    return std::nullopt;
  }

  return canon_joined;
}

std::optional<std::string> ReadFileLimited(const std::filesystem::path& p,
                                          size_t max_bytes,
                                          std::string* err) {
  std::ifstream in(p, std::ios::in | std::ios::binary);
  if (!in.is_open()) {
    if (err)
      *err = "failed to open";
    return std::nullopt;
  }

  std::string buf;
  buf.resize(max_bytes);
  in.read(&buf[0], static_cast<std::streamsize>(max_bytes));
  buf.resize(static_cast<size_t>(in.gcount()));
  return buf;
}

// Best-effort .gitignore-aware path filter.
// For now: respect .gitignore by skipping common ignored dirs and any explicit patterns
// are not implemented (keeps behavior conservative).
bool IsIgnoredPath(const std::filesystem::path& rel) {
  for (const auto& part : rel) {
    const std::string s = part.string();
    if (s == ".git" || s == "build" || s == "third_party" || s == ".cache")
      return true;
  }
  return false;
}

}  // namespace

FileTool::FileTool() {
  ToolSpec::Builder tb;
  tb.SetName("file").SetDescription("Workspace file operations");

  {
    FunctionSpec::Builder fb;
    fb.SetName(kReadName).SetDescription("Read a UTF-8 text file under workspace");
    {
      FieldSpec::Builder p;
      p.SetName("path")
          .SetDescription("Relative path under workspace")
          .SetRequired(true)
          .SetType(TypeSpecImplString::Builder().Build());
      fb.AddParam(std::move(p).Build());
    }
    tb.AddFunction(std::move(fb).Build());
  }

  {
    FunctionSpec::Builder fb;
    fb.SetName(kGlobName).SetDescription("List files under workspace matching a glob pattern");
    {
      FieldSpec::Builder pat;
      pat.SetName("pattern")
          .SetDescription("Glob pattern (relative to workspace), e.g. src/**/*.cc")
          .SetRequired(true)
          .SetType(TypeSpecImplString::Builder().Build());
      fb.AddParam(std::move(pat).Build());
    }
    tb.AddFunction(std::move(fb).Build());
  }

  {
    FunctionSpec::Builder fb;
    fb.SetName(kGrepName).SetDescription("Search for pattern in files under workspace");
    {
      FieldSpec::Builder pat;
      pat.SetName("pattern")
          .SetDescription("Regex pattern")
          .SetRequired(true)
          .SetType(TypeSpecImplString::Builder().Build());
      fb.AddParam(std::move(pat).Build());
    }
    {
      FieldSpec::Builder glob;
      glob.SetName("glob")
          .SetDescription("Optional file glob filter, e.g. **/*.cc")
          .SetRequired(false)
          .SetType(TypeSpecImplString::Builder().Build());
      fb.AddParam(std::move(glob).Build());
    }
    tb.AddFunction(std::move(fb).Build());
  }

  {
    FunctionSpec::Builder fb;
    fb.SetName(kEditName)
        .SetDescription("Replace a literal substring in a file under workspace");
    {
      FieldSpec::Builder p;
      p.SetName("path")
          .SetDescription("Relative path under workspace")
          .SetRequired(true)
          .SetType(TypeSpecImplString::Builder().Build());
      fb.AddParam(std::move(p).Build());
    }
    {
      FieldSpec::Builder old_s;
      old_s.SetName("old")
          .SetDescription("Exact literal text to replace")
          .SetRequired(true)
          .SetType(TypeSpecImplString::Builder().Build());
      fb.AddParam(std::move(old_s).Build());
    }
    {
      FieldSpec::Builder new_s;
      new_s.SetName("new")
          .SetDescription("Replacement text")
          .SetRequired(true)
          .SetType(TypeSpecImplString::Builder().Build());
      fb.AddParam(std::move(new_s).Build());
    }
    {
      FieldSpec::Builder all;
      all.SetName("replace_all")
          .SetDescription("If true, replace all occurrences")
          .SetRequired(false)
          .SetType(TypeSpecImplBoolean::Builder().Build());
      fb.AddParam(std::move(all).Build());
    }
    tb.AddFunction(std::move(fb).Build());
  }

  {
    FunctionSpec::Builder fb;
    fb.SetName(kWriteName)
        .SetDescription(
            "Write a UTF-8 text file under workspace (create or overwrite with overwrite=true)");
    {
      FieldSpec::Builder p;
      p.SetName("path")
          .SetDescription("Relative path under workspace")
          .SetRequired(true)
          .SetType(TypeSpecImplString::Builder().Build());
      fb.AddParam(std::move(p).Build());
    }
    {
      FieldSpec::Builder c;
      c.SetName("content")
          .SetDescription("File content (UTF-8)")
          .SetRequired(true)
          .SetType(TypeSpecImplString::Builder().Build());
      fb.AddParam(std::move(c).Build());
    }
    {
      FieldSpec::Builder o;
      o.SetName("overwrite")
          .SetDescription("If true, allow overwriting an existing file")
          .SetRequired(false)
          .SetType(TypeSpecImplBoolean::Builder().Build());
      fb.AddParam(std::move(o).Build());
    }
    tb.AddFunction(std::move(fb).Build());
  }

  spec_ = std::make_unique<ToolSpec>(std::move(tb).Build());
}

FileTool::~FileTool() = default;

const ToolSpec* FileTool::GetSpec() const {
  return spec_.get();
}

dust::FuturePtr<nlohmann::json> FileTool::Invoke(dust::RefPtr<AgentContext> context,
                                                 const std::string& function_name,
                                                 const nlohmann::json& args) {
  if (!context || !context->session())
    return dust::Just(Err("missing context/session"));

  const std::filesystem::path workspace = context->session()->workspace_path();

  if (function_name == kReadName) {
    std::string e;
    auto abs = ResolveWorkspaceRelative(workspace, args, "path", &e);
    if (!abs)
      return dust::Just(Err(std::move(e)));

    std::error_code ec;
    const auto rel = std::filesystem::relative(*abs, workspace, ec);
    if (!ec && IsIgnoredPath(rel))
      return dust::Just(Err("path is ignored"));

    auto s = ReadFileLimited(*abs, kMaxReadBytes, &e);
    if (!s)
      return dust::Just(Err(std::move(e)));

    nlohmann::json data;
    data["path"] = std::filesystem::relative(*abs, workspace, ec).string();
    data["content"] = *s;
    data["truncated"] = (s->size() >= kMaxReadBytes);
    return dust::Just(Ok(std::move(data)));
  }

  if (function_name == kGlobName) {
    auto pattern = json::GetString(args, "pattern");
    if (!pattern)
      return dust::Just(Err("missing 'pattern'"));

    // Very small glob implementation: only supports prefix + "**/*.ext" or "**/name".
    // For now, enumerate all files and do a simple suffix match.
    std::vector<std::string> out;
    std::error_code ec;
    for (auto it = std::filesystem::recursive_directory_iterator(workspace, ec);
         it != std::filesystem::recursive_directory_iterator();
         it.increment(ec)) {
      if (ec)
        break;
      if (!it->is_regular_file(ec))
        continue;

      auto rel = std::filesystem::relative(it->path(), workspace, ec);
      if (ec)
        continue;
      if (IsIgnoredPath(rel))
        continue;

      const std::string rel_s = rel.generic_string();
      const std::string pat = *pattern;

      bool match = false;
      if (pat == "**/*") {
        match = true;
      } else if (pat.rfind("**/", 0) == 0) {
        const std::string suffix = pat.substr(3);
        if (suffix == "*") {
          match = true;
        } else if (suffix.rfind("*.", 0) == 0) {
          // Support "**/*.ext".
          const std::string ext = suffix.substr(1);  // ".ext"
          if (rel_s.size() >= ext.size() &&
              rel_s.compare(rel_s.size() - ext.size(), ext.size(), ext) == 0)
            match = true;
        } else {
          // Support "**/name" and exact suffix matches.
          if (rel_s.size() >= suffix.size() &&
              rel_s.compare(rel_s.size() - suffix.size(), suffix.size(), suffix) == 0)
            match = true;
        }
      } else {
        // Exact match.
        match = (rel_s == pat);
      }

      if (match) {
        out.push_back(rel_s);
        if (out.size() >= kMaxGlobMatches)
          break;
      }
    }

    nlohmann::json data;
    data["paths"] = out;
    data["truncated"] = (out.size() >= kMaxGlobMatches);
    return dust::Just(Ok(std::move(data)));
  }

  if (function_name == kGrepName) {
    auto pattern = json::GetString(args, "pattern");
    if (!pattern)
      return dust::Just(Err("missing 'pattern'"));

    auto glob = json::GetStringAllowMissing(args, "glob");
    const std::string glob_s = glob ? *glob : "";

    std::vector<nlohmann::json> matches;
    std::error_code ec;
    for (auto it = std::filesystem::recursive_directory_iterator(workspace, ec);
         it != std::filesystem::recursive_directory_iterator();
         it.increment(ec)) {
      if (ec)
        break;
      if (!it->is_regular_file(ec))
        continue;

      auto rel = std::filesystem::relative(it->path(), workspace, ec);
      if (ec)
        continue;
      if (IsIgnoredPath(rel))
        continue;

      const std::string rel_s = rel.generic_string();
      if (!glob_s.empty()) {
        if (glob_s.rfind("**/", 0) == 0) {
          const std::string suffix = glob_s.substr(3);
          if (!(rel_s.size() >= suffix.size() &&
                rel_s.compare(rel_s.size() - suffix.size(), suffix.size(), suffix) == 0))
            continue;
        } else {
          if (rel_s != glob_s)
            continue;
        }
      }

      std::string content;
      auto s = ReadFileLimited(it->path(), kMaxReadBytes, nullptr);
      if (!s)
        continue;
      content = std::move(*s);

      // Note: pattern is treated as a literal substring. Regex is intentionally not supported.
      size_t pos = content.find(*pattern);
      if (pos == std::string::npos)
        continue;

      nlohmann::json m;
      m["path"] = rel_s;
      m["index"] = static_cast<int>(pos);
      matches.push_back(std::move(m));
      if (matches.size() >= kMaxGrepMatches)
        break;
    }

    nlohmann::json data;
    data["matches"] = matches;
    data["truncated"] = (matches.size() >= kMaxGrepMatches);
    return dust::Just(Ok(std::move(data)));
  }

  if (function_name == kEditName) {
    std::string e;
    auto abs = ResolveWorkspaceRelative(workspace, args, "path", &e);
    if (!abs)
      return dust::Just(Err(std::move(e)));

    std::error_code ec;
    const auto rel = std::filesystem::relative(*abs, workspace, ec);
    if (!ec && IsIgnoredPath(rel))
      return dust::Just(Err("path is ignored"));

    auto old_s = json::GetString(args, "old");
    auto new_s = json::GetString(args, "new");
    if (!old_s || !new_s)
      return dust::Just(Err("missing 'old' or 'new'"));

    bool replace_all = false;
    if (args.contains("replace_all") && args["replace_all"].is_boolean())
      replace_all = args["replace_all"].get<bool>();

    auto content = ReadFileLimited(*abs, kMaxReadBytes, &e);
    if (!content)
      return dust::Just(Err(std::move(e)));

    size_t count = 0;
    std::string out;
    const std::string& in = *content;
    const std::string& needle = *old_s;
    if (needle.empty())
      return dust::Just(Err("'old' must be non-empty"));

    size_t start = 0;
    while (true) {
      size_t pos = in.find(needle, start);
      if (pos == std::string::npos) {
        out.append(in, start, std::string::npos);
        break;
      }
      out.append(in, start, pos - start);
      out.append(*new_s);
      ++count;
      start = pos + needle.size();
      if (!replace_all) {
        out.append(in, start, std::string::npos);
        break;
      }
    }

    if (count == 0)
      return dust::Just(Err("no match"));

    // Overwrite in place (Write is not a separate tool function).
    std::ofstream f(*abs, std::ios::out | std::ios::binary | std::ios::trunc);
    if (!f.is_open())
      return dust::Just(Err("failed to open for write"));
    f.write(out.data(), static_cast<std::streamsize>(out.size()));

    nlohmann::json data;
    data["path"] = rel.generic_string();
    data["replaced"] = static_cast<int>(count);
    return dust::Just(Ok(std::move(data)));
  }

  if (function_name == kWriteName) {
    std::string e;
    auto abs = ResolveWorkspaceRelative(workspace, args, "path", &e);
    if (!abs)
      return dust::Just(Err(std::move(e)));

    std::error_code ec;
    const auto rel = std::filesystem::relative(*abs, workspace, ec);
    if (!ec && IsIgnoredPath(rel))
      return dust::Just(Err("path is ignored"));

    auto content = json::GetString(args, "content");
    if (!content)
      return dust::Just(Err("missing 'content'"));
    if (content->size() > kMaxWriteBytes)
      return dust::Just(Err("content too large"));

    bool overwrite = false;
    if (args.contains("overwrite") && args["overwrite"].is_boolean())
      overwrite = args["overwrite"].get<bool>();

    if (std::filesystem::exists(*abs, ec)) {
      if (std::filesystem::is_directory(*abs, ec))
        return dust::Just(Err("path is a directory"));
      if (!overwrite)
        return dust::Just(Err("file exists (set overwrite=true)"));
    }

    // Ensure parent directories exist.
    std::filesystem::create_directories(abs->parent_path(), ec);
    if (ec)
      return dust::Just(Err("failed to create parent directories"));

    std::ofstream f(*abs, std::ios::out | std::ios::binary | std::ios::trunc);
    if (!f.is_open())
      return dust::Just(Err("failed to open for write"));
    f.write(content->data(), static_cast<std::streamsize>(content->size()));

    nlohmann::json data;
    data["path"] = rel.generic_string();
    data["bytes"] = static_cast<int>(content->size());
    data["overwrote"] = overwrite;
    return dust::Just(Ok(std::move(data)));
  }

  return nullptr;
}

}  // namespace agent
