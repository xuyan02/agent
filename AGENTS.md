## Mi Code Added Memories
- User prefers Plan Tool implemented as a new plan2 system (do not modify existing infra/plan), with UUID visible to LLM and in tool arguments; top-level tasks must include report_to explicitly; add_tasks auto-computes initial status; Markdown render uses v3 indentation with id on its own line and children block.
- In the parallel tool-call design discussion, user defines had_tool_calls_ to be set true on OnToolCalls arrival and reset to false in OnRequestDone (same round).
