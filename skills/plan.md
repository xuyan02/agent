#Plan

Make a **small, outcome-driven plan** and keep it updated.

## Hard rules (format + invariants)
- Top-level tasks must set `report_to` (no default). Children must not set `report_to`.
- Dependencies are only among siblings. Status must be consistent with dependencies.
- At most **one** top-level task may be `in_progress`.
- A `blocked` task must not be set to `in_progress` (unblock first).
- Each top-level task description must include **DoD** (Definition of Done): an explicit, verifiable completion condition.
- A `blocked` top-level task must record (in its description or first child): **who** you wait for, **what** you wait for, and the **unblock condition**.

## Per-round loop (L1–L9)
Use this decision order every round:

L1) **Parse & triage input**
- Split input into: **need-to-handle** (requires any action: reply / plan update / tools / assignment / execution) vs **no-action**.

L2) **Handle unblock signals**
- If input satisfies a blocked task’s unblock condition, update its status to unblock it (typically `pending`).

L3) **Handle need-to-handle messages**
- If you can handle and reply quickly: do it and reply.
- Otherwise: create/update a **top-level task** to track it (with DoD).

L4) **Ensure an `in_progress` task**
- If none exists, choose the highest-priority `pending` task and set it `in_progress`.
- Priority (high → low):
  1) just unblocked by latest input
  2) explicitly requested by latest input
  3) earlier in a dependency chain
  4) other pending

L5) **Execute only the `in_progress` task**
- Do not do unrelated work.

L6) **Decompose large work when needed**
- If the `in_progress` task is clearly not quickly finishable, or has multiple distinct deliverables/verification points, add child steps.
- Minimal child-step expectations:
  - reflect distinct deliverables/verification/risk points
  - each child is small enough for one continuous push
  - if DoD includes verification, include at least one verification/check child

L7) **Block only for explicit waiting**
- Set `blocked` only when explicitly waiting for a reply/artifact/decision from another party.
- Record who/what/unblock condition.

L8) **Complete & report (mandatory)**
- When a top-level task completes: mark it `completed` immediately.
- In the **same round**, report to its `report_to`.
- Report content requirement: include **what was completed** (one natural-language sentence is enough).

L9) **No need-to-handle messages => internal progress**
- By default, emit no `@to:` output.
- Advance the plan.
- Only emit `@to:` for: completion report, clarification, blocked, or failure.

## Switching `in_progress`
You may switch which task is `in_progress` when input requires it.
- The previous `in_progress` task must be set back to `pending`.
- No reason note is required.

Switch triggers (any):
- input explicitly requests prioritizing another objective
- input changes scope such that a prerequisite should be done first
- input unblocks a blocked task that becomes highest priority

## Failure handling
After a failure:
- capture the reason
- then either re-plan under current constraints, or declare impossible and inform `report_to`

## Auto-drive & control frames (`[resume]` / `[pause]`)
- The runtime may send `[resume]` ticks when there is no new external input.
- Hybrid (3c) update strategy:
  - update the plan on meaningful events (start/complete/blocked/scope change)
  - additionally, on `[resume]` ticks, do a lightweight plan check every **3** ticks (only update if something changed)

### When to output `[pause]`
Output `[pause]` when all are true:
- this round has no need-to-handle messages
- there are no executable plan steps (no `pending` to start and no `in_progress` to advance)
- there is no completed-but-not-reported top-level task

`[pause]` must not be combined with any `@to:` output.
