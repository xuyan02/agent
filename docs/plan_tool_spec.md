# Plan Tool Spec (Draft)

This document specifies the **Plan Tool** provided by each `GeneralAgent` instance.

## 1. Model

### 1.1 Task

A task supports:
- parent/children (tree)
- same-level dependencies (must share the same parent)
- report-to target (top-level tasks only)
- status transitions with cascading effects

Fields:
- `id: string` (UUID)
- `title: string`
- `detail: string` (optional)
- `parent_id: string` (optional UUID)
- `depends_on: string[]` (optional UUID list; **siblings only**, i.e. same `parent_id`)
- `report_to: string` (required for **top-level** tasks; forbidden for non-top-level tasks)
- `status: Status`
- `block_reason: string` (required iff `status == blocked`)

### 1.2 Status

Non-terminal:
- `pending`: ready to start but not started (dependencies satisfied)
- `not_ready`: dependencies are not satisfied; cannot start
- `blocked`: external conditions prevent start/continue (dependencies must be satisfied); requires `block_reason`
- `in_progress`: started

Terminal (no rollback):
- `done`
- `canceled`
- `failed`

Dependency constraints:
- `in_progress`, `pending`, `blocked` require dependencies satisfied.
- `not_ready` is only valid when dependencies are not satisfied.

Cascading:
- When a task becomes `canceled` or `failed`, all downstream tasks (transitive, within same-level dependency graph) become `canceled`.

Parent subtree deletion:
- When a **parent task** enters any terminal status, its entire subtree is **hard-deleted**.

Deletion API cascades deletion:
- `plan.remove_task` deletes the task subtree and also deletes downstream dependent subtrees.

---

## 2. Tool API (OpenAI built-in tool calling)

Tool: `plan`

Functions exposed (MVP):
- `plan.add_tasks`
- `plan.set_status`
- `plan.remove_task`

### 2.1 plan.add_tasks

Arguments:

```json
{
  "tasks": [
    {
      "title": "string",
      "detail": "string (optional)",
      "parent_id": "string (optional uuid)",
      "depends_on": ["string (uuid)"] ,
      "report_to": "string (required iff parent_id is absent)"
    }
  ]
}
```

Validation:
- If `parent_id` is absent (top-level): `report_to` is required and must be provided by the LLM (no default).
- If `parent_id` is present (subtask): `report_to` must not be provided.
- `depends_on` items must be siblings: same `parent_id` as the task.

Status initialization:
- The tool computes the initial `status` automatically:
  - `not_ready` if dependencies are not satisfied.
  - otherwise `pending`.
- Creating a task in `blocked/in_progress/terminal` is not supported by `plan.add_tasks`.

Result (diff-only, no UUID sid mapping):

```json
{
  "created": [
    {
      "id": "uuid",
      "title": "...",
      "status": "pending|not_ready|blocked",
      "parent_id": "uuid (optional)",
      "depends_on": ["uuid"],
      "report_to": "master|agent_name (top-level only)",
      "block_reason": "... (only when blocked)"
    }
  ],
  "warnings": ["..."]
}
```

No support for same-call temporary references; parent tasks must be created first.

### 2.2 plan.set_status

Arguments:

```json
{
  "task_id": "string (uuid)",
  "status": "pending|not_ready|blocked|in_progress|done|canceled|failed",
  "block_reason": "string (required iff status==blocked)"
}
```

Rules:
- Terminal statuses cannot roll back.
- Setting to `pending` requires dependencies satisfied; otherwise error.
- Setting to `not_ready` is not allowed if dependencies are satisfied; otherwise error.
- Setting to `blocked` requires dependencies satisfied and `block_reason`.
- Setting to `in_progress` requires dependencies satisfied.
- Setting to terminal (`done/canceled/failed`) is allowed.
- If status becomes `canceled` or `failed`: downstream tasks become `canceled` (transitive).
- If a **parent** enters terminal: hard-delete its whole subtree.

Result:

```json
{
  "updated": [{"id":"uuid","status":"..."}],
  "canceled_cascade": [{"id":"uuid","status":"canceled"}],
  "deleted": ["uuid"],
  "normalized": [{"id":"uuid","status":"pending"}],
  "warnings": ["..."]
}
```

### 2.3 plan.remove_task

Arguments:

```json
{
  "task_id": "string (uuid)"
}
```

Rules:
- Hard-delete the task's entire subtree.
- Also hard-delete downstream dependent subtrees (transitive).

Result:

```json
{
  "deleted": ["uuid"],
  "warnings": ["..."]
}
```

---

## 3. Plan Markdown Rendering (for system prompt)

### 3.1 Requirements
- Show parent/child relationships clearly.
- Show `id` on its own line.
- Include `report_to` on top-level tasks only.
- Include `depends_on` (same-level dependencies).
- Include `block_reason` when status is `blocked`.
- Deterministic ordering: insertion/creation order within each sibling list.

### 3.2 Indentation spec (v3)

Let `depth=0` be top-level tasks. Use **2 spaces per depth** for task header indentation.

For a task at `depth`:
- Header line indent = `2 * depth` spaces.
  - Format: `- [<status>] <title>`

- Field lines indent = `2 * depth + 4` spaces.
  - Field lines are bullet items:
    - `- id: <uuid>`
    - `- report_to: <name>` (top-level only)
    - `- depends_on: <uuid, uuid> | -`
    - `- detail: ...` (optional)
    - `- block_reason: ...` (only when blocked)

- Children section:
  - `children:` line indent = `2 * depth + 4` spaces.
    - Format: `- children:`
  - Children task headers indent = `2 * depth + 8` spaces (i.e. one extra level under `children:`).

### 3.3 Example

```md
## Plan
- [pending] Top task title
    - id: 3b2f...
    - report_to: master
    - depends_on: -
    - detail: ...
    - children:
        - [in_progress] Subtask title
            - id: 9a1c...
            - depends_on: -
            - detail: ...
- [blocked] Another top task
    - id: 1c7d...
    - report_to: agent_name
    - depends_on: 3b2f...
    - block_reason: waiting for credentials
```
