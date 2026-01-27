# Skill System Design

## Goal
Introduce a **Skill** system that allows the Runtime to load skills from disk and allows agents (specifically `GeneralAgent`) to inject skill prompts into the LLM **system** message.

Key requirements from discussion:
- Each skill has: **name**, **description**, **prompt**.
- Each skill is stored as a **JSON** file, with the **prompt stored in a separate Markdown (.md)** file.
- Runtime scans a fixed directory: **`<project_root>/skills/`**.
- **No activation/deactivation API in MVP.**
- `general_agent` is a `GeneralAgent` **default skill** and is **always active** (it does not live in any activation list).
- Base `Agent` should not know about skills; it only provides a “system prompt” via a **virtual method**.
  - Add `virtual std::string GetSystemPrompt() const;` (default empty).
  - Subclasses decide how to build it.
- OpenAI streaming request must send **two messages**: system + user.
- Error-handling policy (chosen):
  - (1B) If skills directory missing: print a **warning**.
  - (2A) If a single skill is invalid/missing files: **skip and continue**.
  - (3A) If `GeneralAgent` cannot load `general_agent` skill: print an **error** and continue without it.
  - (4B) System prompt concatenation includes a header **`[Skill: <name>]`** before each prompt.

Non-goals (for initial version):
- Hot reload / file watching.
- Persisting active skill list to disk.
- Sophisticated CLI command language.

---

## On-disk format

### Skill JSON schema
File: `<project_root>/skills/<anything>.json`

```json
{
  "name": "shell",
  "description": "Run shell commands safely and summarize results.",
  "prompt_md": "shell.md"
}
```

Notes:
- `prompt_md` is a path **relative to `<project_root>/skills/`**.
- `name` must be unique across loaded skills.

### Skill prompt markdown
File: `<project_root>/skills/shell.md`

Content is used verbatim as the skill prompt. No frontmatter required.

---

## Runtime responsibilities

### SkillRegistry
Add a registry owned by `Runtime`:

- Holds all loaded skills keyed by `name`.
- Provides lookup and listing APIs.

Proposed interface (exact naming can follow local conventions):

```cpp
struct Skill {
  std::string name;
  std::string description;
  std::string prompt_md;   // fully loaded markdown content
  std::string json_path;   // for diagnostics
  std::string md_path;     // for diagnostics
};

class SkillRegistry {
public:
  bool LoadFromDir(const std::filesystem::path& skills_dir);

  const Skill* Find(const std::string& name) const;
  std::vector<std::string> ListNamesSorted() const;

private:
  std::unordered_map<std::string, Skill> by_name_;
};
```

### Scanning rules
- Skills directory: `skills_dir = project_root / "skills"`.
- Scan for `*.json` in that directory (non-recursive initially).
- For each JSON file:
  - parse required fields `name`, `description`, `prompt_md`.
  - read `skills_dir / prompt_md` into memory.
  - register skill by `name`.

### Error handling

#### Missing skills directory (1B)
- If `skills_dir` does not exist:
  - print: `warning: skills dir not found: <path>`
  - treat as “no skills loaded”.
  - return `true` from `LoadFromDir` (so app can run).

#### Invalid skill file (2A)
If a single skill JSON is invalid or its md file cannot be read:
- print a single-line `error:` describing which file and why.
- skip it, continue scanning others.

#### Duplicate skill names
- Treat duplicates as error: keep the first one loaded, skip later duplicates with `error:`.

---

## Agent responsibilities

### Base Agent change
Add a virtual method to the base class:

```cpp
class Agent {
public:
  // ...
  virtual std::string GetSystemPrompt() const { return {}; }

protected:
  bool StartLlmRequest(std::string model,
                      std::string user_prompt,
                      agent::LlmRequest::OnToken on_token,
                      agent::LlmRequest::OnDone on_done);
};
```

**Important:** Base `Agent` does not know skills or registry.

### GeneralAgent default skill: `general_agent` (always active)
In MVP, `GeneralAgent` does not expose an activation list or activation/deactivation APIs.

Instead, `GeneralAgent::GetSystemPrompt()` always attempts to include exactly one default skill:
- `general_agent`

### Building system prompt (4B)
`GeneralAgent::GetSystemPrompt()`:
- Lookup `SkillRegistry` via `runtime()`.
- If the default skill exists, return:

```
[Skill: general_agent]
<skill.prompt_md>

---

```

If the default skill is missing (2A/3A):
- print: `error: missing default skill: general_agent`
- return empty system prompt.

### Request building in GeneralAgent
When creating the LLM request:
- `user_prompt` is the existing `BuildAgentBatchInput(&queue_)` output.
- `system_prompt` is `GetSystemPrompt()`.

System prompt injection happens at request creation, not during streaming.

---

## LLM API changes

### LlmContext / Provider
Currently, OpenAI provider only accepts one `prompt` string and sends it as a single `user` message.

To support B (system+user), extend the request creation path to take both:

- `system_prompt` (may be empty)
- `user_prompt` (required)

Proposed API shape:

```cpp
class LlmContext {
public:
  std::unique_ptr<LlmRequest> Create(std::string model,
                                    std::string system_prompt,
                                    std::string user_prompt,
                                    LlmRequest::OnToken,
                                    LlmRequest::OnDone);
};
```

Providers that don’t support system messages may:
- concatenate system+user (fallback) OR
- ignore system (not recommended)

For OpenAI provider, it should map to:

```json
"messages": [
  {"role":"system","content":"<system_prompt>"},
  {"role":"user","content":"<user_prompt>"}
]
```

### OpenAI JSON escaping
Both `system_prompt` and `user_prompt` must be JSON-escaped.

---

## CLI / UX (optional, not required for MVP)
This design does not require new CLI commands, but a future addition could include:
- `/skills` list
- `/skill on <name>`
- `/skill off <name>`

---

## Test plan

1) SkillRegistry loading
- Load a temp `skills/` dir with a valid `*.json` + `*.md`.
- Assert `Find(name)` returns correct description and prompt content.
- Missing skills dir emits warning (can be asserted by behavior if stderr capture exists; otherwise just ensure no crash and empty list).

2) Invalid skill files
- Bad JSON / missing required fields / missing md: ensure it logs error and continues.

3) GeneralAgent system prompt composition
- Ensure `GetSystemPrompt()` includes the `general_agent` default skill with `[Skill: general_agent]` header.
- Ensure missing `general_agent` skill results in an `error:` line and empty system prompt.

4) OpenAI request body
- Ensure body contains two messages with roles system/user and both are escaped (no raw newlines).

---

## Migration steps

1) Add `SkillRegistry` and load it during app wiring (when Runtime is created and config has `project_root`).
2) Add `Agent::GetSystemPrompt()` and plumb system_prompt through `Agent::StartLlmRequest` -> `LlmContext::Create` -> provider.
3) Update OpenAI provider request body to send system+user.
4) Extend `GeneralAgent` to inject the `general_agent` default skill prompt.
5) Add tests.
