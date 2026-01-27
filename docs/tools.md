# Tool System Design

## Goal
Introduce a tool system that integrates with **OpenAI built-in tool calling**.

Key requirements:
- Distinguish **Tool** vs **Function**:
  - **Function** is the executable interface and carries an OpenAI function spec.
  - **Tool** is a collection of one or more Functions.
- Tools can be provided at multiple levels: **Runtime / Team / Agent**.
- Tool activation is decided **only by `GeneralAgent`**, via `GetActiveTools()`.
- The LLM request receives a list of **active Tools**; expansion into OpenAI `tools` format happens inside the LLM request/provider.
- Specs use **OpenAI native format**.
- Tool id conflicts: **error + ignore later** (policy 1A).

Non-goals (initially):
- Security policy / sandboxing integration.
- Implementing the full tool execution loop (streaming tool_calls parsing + invoking + continuing).

---

## Concepts

### Function
A Function is the smallest executable unit.

Properties:
- Must have a globally unique `name` (e.g. `file.read`, `shell.run`).
- Provides an OpenAI-compatible schema.

Proposed interface:

```cpp
struct FunctionSpec {
  std::string name;            // globally unique
  std::string description;

  // OpenAI native JSON schema for arguments, serialized as a string that
  // represents a JSON object.
  std::string parameters_json;
};

class Function {
public:
  virtual ~Function() = default;
  virtual const FunctionSpec& spec() const = 0;

  // arguments_json: JSON object string matching parameters schema.
  // out_result_json: JSON object string.
  virtual bool Invoke(std::string arguments_json,
                      std::string* out_result_json,
                      std::string* out_error) = 0;
};
```

### Tool
A Tool is a named collection of Functions. Tool activation is done by tool id.

```cpp
struct Tool {
  std::string id;                 // activation unit
  std::string description;        // optional
  std::vector<std::shared_ptr<Function>> functions;
};
```

Constraints:
- `Tool.id` must be unique within the aggregated tool list for a request.
- If the same `Tool.id` is provided from multiple levels, this is an error and
  later tools are ignored (policy 1A).

---

## Providing Tools (Runtime / Team / Agent)

All three levels expose the same method name:

```cpp
std::vector<Tool> GetTools() const;
```

Semantics:
- `Runtime::GetTools()` returns runtime-level tools.
- `Team::GetTools()` returns team-level tools (team-wide).
- `Agent::GetTools()` returns agent-level tools (default empty).

Notes:
- `GetTools()` only provides tool definitions; it does not decide activation.

---

## Activating Tools (GeneralAgent only)

`GeneralAgent` decides tool activation:

```cpp
std::vector<std::string> GetActiveTools() const;
```

MVP behavior:
- Hard-coded within `GeneralAgent`.

---

## Aggregation (GeneralAgent)

Before creating an LLM request, `GeneralAgent` aggregates tools from all levels:

1. Collect:
   - `runtime().GetTools()`
   - `team().GetTools()`
   - `this->GetTools()`

2. Deduplicate by `Tool.id`:
   - If a duplicate id is found: print `error:` and ignore the later one.

3. Filter active tools:
   - `active_ids = GetActiveTools()`
   - active list = tools where `tool.id` is in `active_ids`.

4. Pass active tools into LLM request creation.

---

## LLM Request / Provider integration

### Request creation
The Agent passes:
- `system_prompt`
- `user_prompt`
- `active_tools: std::vector<Tool>`

### Expansion to OpenAI native format
Expansion happens inside the LLM layer (request/provider).

For Chat Completions, the provider should include:

```json
"tools": [
  {
    "type": "function",
    "function": {
      "name": "file.read",
      "description": "...",
      "parameters": { /* JSON schema */ }
    }
  }
]
```

Notes:
- `FunctionSpec::parameters_json` must be emitted as a JSON object.
- `FunctionSpec::name` must be unique across the expanded list.

---

## Error handling

- Duplicate Tool id: `error:` + ignore later.
- Duplicate Function name (across active tools): `error:` + ignore later (or fail request).
  - MVP recommendation: `error:` + ignore later.

---

## MVP implementation steps

1. Add `Function`/`Tool` model types.
2. Add `GetTools()` to Runtime/Team/Agent (default empty).
3. Add `GetActiveTools()` to GeneralAgent.
4. Implement GeneralAgent aggregation + filtering and plumb active tools into LLM request creation.
5. Implement provider-side expansion into OpenAI `tools` JSON field.

(Execution loop for tool calls is a follow-up.)
