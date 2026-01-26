# Agent Architecture (Runtime / Team / Agent)

This document defines the **core runtime architecture** and the public APIs for:

- `Runtime`: message routing + unified message emission.
- `Team`: owns/manages multiple `GeneralAgent` instances.
- `Agent`: base class (owns `Runtime&` + `name`).
- `GeneralAgent`: multi-party agent (can talk to multiple targets).
- `ProfessionalAgent`: one-shot single-input/single-output agent.

Design goals / constraints (confirmed):

- Keep the design minimal: **no extra "event" types** like `InputSource` / `ConversationTarget`.
- `Message` has **only** `from/to/content` (no `source`).
- `Team` has no "active agent" concept. All agents are equal.
- A `leader` agent exists and is fixed by config (`team.json`) (L1).
- CLI input represents a user named **`master`**.
- Explicit routing syntax: `@name: <payload>`
  - Parsing happens in `Runtime`.
  - `<payload>` may be empty.
- `Runtime` is driven by `dust::MessageLoop`.
  - `Runtime` MUST NOT store/pass `MessageLoop*` or references.
  - Access the loop via `dust::MessageLoop::Current()`.
- Error reporting policies:
  - Missing/invalid `to` in agent output: drop and log to **stderr** only.
  - Unknown `to` at `Runtime::Emit`: drop and log to **stderr** only.

---

## 1. Message

Input and output are both represented as `Message`.

```cpp
struct Message {
  // Sender name.
  // - User: "master"
  // - Agent: "<agent_name>"
  std::string from;

  // Target name.
  // - User: "master"
  // - Agent: "<agent_name>"
  std::string to;

  // Payload.
  std::string content;
};
```

---

## 2. Agent (base class)

- `Agent` owns a reference to its `Runtime`.
- `Agent` owns its `name`.

```cpp
class Runtime;

class Agent {
public:
  Agent(Runtime& runtime, std::string name)
      : runtime_(runtime), name_(std::move(name)) {}
  virtual ~Agent() = default;

  std::string name() const { return name_; }

protected:
  Runtime& runtime() { return runtime_; }
  const Runtime& runtime() const { return runtime_; }

private:
  Runtime& runtime_;
  std::string name_;
};
```

---

## 3. GeneralAgent

`GeneralAgent` is the multi-party agent:

- It receives full `Message` objects.
- It may emit messages to multiple targets.
- Output messages MUST specify `to`.
- Output messages MAY specify multiple targets using a textual multi-target
  protocol (see below).

```cpp
class GeneralAgent final : public Agent {
public:
  using Agent::Agent;
  ~GeneralAgent() override = default;

  // Enqueue and/or handle an incoming message.
  // The message is addressed to this agent: msg.to == this->name().
  void Input(const Message& msg);

private:
  // impl components...
};
```

### 3.1 Delivery model (queue + single in-flight request)

GeneralAgent maintains an internal `std::deque<Message>` and an `in_flight` flag.

Confirmed scheduling rules:

- `Input(msg)` always enqueues `msg`.
- If `in_flight == true`: return immediately.
- If `in_flight == false`:
  - **Batch1**: drain the whole queue into a batch.
  - Build a single LLM input string from the batch.
  - Start one LLM request (`in_flight=true`).
- When the LLM request completes:
  - set `in_flight=false`.
  - `PostTask` to try scheduling again (if the queue is non-empty).

### 3.2 Batch input formatting

When starting a request, the drained batch is converted into a single string:

```
@from1: content
@from2: content
...
```

Notes:

- `to` is not included because all queued messages target this agent.

### 3.3 Multi-target output protocol (inside GeneralAgent)

GeneralAgent output is allowed to target multiple recipients.

Protocol:

- A line starting with `@to: rest` begins a new output block for `to`.
- Lines not starting with `@` are treated as **continuation lines** for the
  current `to` (P2).
- If a continuation line appears before any `@to:` header, drop the line and
  log to stderr.
- If a line starts with `@` but is not a valid `@to:` header, drop the line and
  log to stderr.

GeneralAgent must parse its generated output and call `Runtime::Emit(...)`
**once per target block**:

```cpp
runtime().Emit(Message{.from = name(), .to = to, .content = content});
```

If `to` is missing/empty for a block, that block must be dropped and logged to
stderr.

---

## 4. ProfessionalAgent

`ProfessionalAgent` is single-input / single-output:

- Input takes only `content`.
- Output is delivered via a callback that receives only `content`.

```cpp
class ProfessionalAgent final : public Agent {
public:
  using ReplyFn = dust::Function<void(const std::string& content)>;

  using Agent::Agent;
  ~ProfessionalAgent() override = default;

  // Single input -> single output.
  void Input(const std::string& input, ReplyFn reply);

private:
  // impl components...
};
```

---

## 5. Team

`Team` manages multiple `GeneralAgent` instances.

- Agents are equal.
- `leader` is fixed by configuration.

### 5.1 team.json schema (Cfg1)

```json
{
  "leader": "leader",
  "agents": [
    {"name": "leader"},
    {"name": "planner"}
  ]
}
```

Constraints:

- `leader` must exist in `agents`.
- `agents[*].name` must be unique and non-empty.

### 5.2 Team API

```cpp
class Team {
public:
  // Loads team.json, creates GeneralAgent instances, and validates the leader.
  // On failure, returns nullptr.
  static std::unique_ptr<Team> Load(Runtime& runtime, const std::string& path);

  // Saves team metadata (leader + agent names) back to team.json.
  // Returns false on failure.
  bool Save(const std::string& path) const;

  explicit Team(Runtime& runtime);

  // Adds an already constructed agent. Returns false on duplicate/invalid name.
  bool Add(std::unique_ptr<GeneralAgent> agent);

  // Lookup for routing.
  GeneralAgent* Find(const std::string& name);

  std::string leader() const;

private:
  Runtime& runtime_;
  std::unordered_map<std::string, std::unique_ptr<GeneralAgent>> agents_;
  std::string leader_;
};
```

---

## 6. Runtime

`Runtime` is the message router and the unified emission point.

### 6.1 Ownership / initialization

- Runtime is constructed without a Team.
- Team is injected later with `SetTeam(std::unique_ptr<Team>)` (S1).
- Runtime owns the Team for its lifetime.

```cpp
class Runtime {
public:
  explicit Runtime(cpp_agent::interfaces::IConsole& console);

  void SetTeam(std::unique_ptr<Team> team);

  // CLI entrypoint: parse routing and deliver the resulting message.
  void OnCliLine(const std::string& line);

  // Unified output channel.
  void Emit(const Message& msg);

private:
  void DeliverToAgent(const Message& msg);

  cpp_agent::interfaces::IConsole& console_;
  std::unique_ptr<Team> team_;
};
```

### 6.2 CLI routing (B + Runtime parses)

Rules:

- If `line` matches `@name: payload`:
  - target = `name`
  - content = `payload` (may be empty)
- Otherwise:
  - target = `team_->leader()`
  - content = `line`

Runtime delivers:

```cpp
DeliverToAgent(Message{.from = "master", .to = target, .content = content});
```

### 6.3 Emit routing (R2 + U1)

Rules:

- If `msg.to == "master"`: print `msg.content` to the console.
- Else if `msg.to` matches a loaded agent name:
  - Deliver to that agent by calling its `Input(msg)`.
  - Important: the agent must only enqueue and schedule processing.
- Else:
  - Drop and log to stderr (U1).

---

## 7. Logging

All protocol/format errors are logged to **stderr** only:

- GeneralAgent output block without a valid `to`.
- Invalid multi-target header lines.
- Continuation line without a prior `@to:` header.
- Runtime emits to an unknown `to`.
