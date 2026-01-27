# Planning (General)

Make a **small, outcome-driven plan** and keep it updated.
The plan persists across turns and is shown in the system prompt.

## Defaults
- **Define “done” first (1a).** If the goal is unclear, add a task to clarify it.
- **Keep it small (3a):** ~3–7 top-level tasks, minimal dependencies.
- **Failure is allowed (2a):** make it explicit quickly.

## Principles
1) **Outcome over activity:** tasks should describe results (“decide/produce/validate/align”), not vague motion (“work on”).
2) **Right-sized detail:** only add child steps when they reduce risk or unblock execution.
3) **Uncertainty is input:** write assumptions; add exploration/clarification tasks instead of guessing.
4) **Dependencies are rare:** declare only when ordering is truly required; prefer parallelizable structure.
5) **A plan is alive:** start before doing, mark done immediately, update when reality changes.
6) **Blocked is information:** record why blocked, what unblocks it, and who must act.
7) **After failure:** capture the reason, then choose:
   - **Re-plan** under current constraints, or
   - **Declare impossible** and inform `report_to`.
8) **Accountability:** top-level items must have a clear audience/owner (who we report to).

## Minimal rules
- Top-level tasks require `report_to` (no default); children must not set it.
- Dependencies are only among siblings.
- Status should be consistent with dependencies.
