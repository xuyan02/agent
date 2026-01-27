# Plan Tool

## Purpose
When the user asks for implementation work (bugfix/feature/refactor/tests), maintain a structured plan using the `plan` tool functions.

The plan is persisted across turns and will be shown to you in the system prompt.

## When to Use
- Use the plan tool for any non-trivial work (3+ steps, multiple files, tests, refactors).
- Update the plan as you discover new sub-tasks or blockers.

## Tool Overview
Available functions:
- `plan.add_tasks`
- `plan.set_status`
- `plan.remove_task`

### Task Modeling Rules
- Each task has a UUID id.
- Top-level tasks MUST include `report_to` (no default). Non-top-level tasks MUST NOT set `report_to`.
- `depends_on` is only allowed among siblings (same parent). Cycles are rejected.
- Statuses:
  - non-terminal: `pending`, `not_ready`, `blocked`, `in_progress`
  - terminal: `done`, `canceled`, `failed`
- Dependency gating:
  - `pending`/`blocked` require all dependencies satisfied.
  - `not_ready` is only allowed when dependencies are NOT satisfied.
- When a task becomes `failed` or `canceled`, downstream dependents are canceled transitively.
- When a task enters a terminal state, its children subtree is hard-deleted (the parent remains).

## Operational Guidance
1. Start by creating a small set of top-level tasks. Each top-level task must include `report_to`.
2. Add child tasks for details (children must omit `report_to`).
3. Before starting work on a task, set it to `in_progress`.
4. After finishing, set it to `done`.
5. If blocked, set status to `blocked` with a clear `block_reason`.

## Examples

### Create a plan
Call `plan.add_tasks` with an array of tasks:
- `title` required
- `detail` optional
- `report_to` required for top-level
- `parent_id` optional (omit for top-level)
- `depends_on` optional (sibling ids)

### Update progress
- `plan.set_status` with `{task_id, status}`.
- For `blocked`, include `block_reason`.

### Remove tasks
- `plan.remove_task` hard-deletes that task subtree and any downstream dependent subtrees.
