# Design: Parallel tool-call execution in GeneralAgent

> Status: draft for discussion

## Goals

- Execute tool calls in parallel as soon as they arrive from the LLM stream.
- Avoid sending any *new* LLM request while tool calls from the current round are still pending.
- When tool calls exist, send the tool results back to the LLM only after:
  - the LLM stream has finished for the current round (OnRequestDone), and
  - all tool calls from the current round have completed.
- Do not start a new “input-queue assembled” request while the current input chain is still in flight.

## Non-goals

- Multi-threaded concurrency control (the program is single-threaded).
- Deduplication of tool_call_id within a round (assumed not to repeat).

## Core invariants / assumptions

1. **Single-threaded event loop**: callbacks do not execute concurrently.
2. **No OnToolCalls after OnRequestDone** for the same LLM request/round.
3. **No new LLM request is sent until all tool calls are completed** once any tool call is observed in a round.
4. Each tool call completes exactly once; `pending_tool_call_count_` matches the number of outstanding completions.

## State variables

Only the following variables are required for the control flow:

- `in_flight_` (bool): whether an *input-chain* is in progress.
  - Set to `true` immediately after sending an LLM request assembled from the external input queue.
  - Remains `true` across any tool-call reply requests.
  - Set to `false` only when the chain reaches a **final text round**.

- `pending_tool_call_count_` (size_t): outstanding tool calls for the current round.
  - Incremented by `OnToolCalls(batch).size()`.
  - Decremented by each tool completion callback.

- `had_tool_calls_` (bool): dual-purpose flag for the current round.
  - `true` means this round observed at least one tool call.
  - `false` is used as the signal that **OnRequestDone has occurred** *after* tool calls were observed.

  **Important**: in `OnRequestDone`, we first branch on the old value of `had_tool_calls_`, and then clear it.

> Rationale: With invariants (2) and (3), `had_tool_calls_` can safely double as "saw tool calls" and "LLM done" signal.

## History recording

### When tool calls arrive

On each `OnToolCalls(batch)` callback:

- Append one assistant history message whose `tool_calls` equals exactly `batch`.
  - This is **per-batch** (not merged).
- For each tool call in `batch`, start tool execution immediately (parallel).

### When each tool finishes

- Append one tool history message with the tool result, referencing `tool_call_id`.

## Decision logic

### OnToolCalls(batch)

- `had_tool_calls_ = true`
- `pending_tool_call_count_ += batch.size()`
- `history += assistant(tool_calls=batch)`
- Start each tool call execution immediately.

### Tool completion callback

- Append tool result to history.
- `pending_tool_call_count_--`
- If `pending_tool_call_count_ == 0` **and** `had_tool_calls_ == false`:
  - Send tool-call reply request: `StartLlmRequest(history)`.
  - This request is a tool-reply request and **must not** depend on external input queue.

### OnRequestDone

First, branch on the old value of `had_tool_calls_`:

- If `had_tool_calls_ == false`:
  - This is a **final text round**.
  - End the chain: `in_flight_ = false`.
  - The next input-chain (if any) may start.

- Else (`had_tool_calls_ == true`):
  - This round is a **tool-call round**.
  - Clear `had_tool_calls_ = false` to signal that LLM is done for this tool-call round.
  - If `pending_tool_call_count_ == 0`:
    - Send tool-call reply request immediately: `StartLlmRequest(history)`.
  - Otherwise:
    - Do nothing; the last tool completion callback will trigger the tool-call reply.

## Busy semantics

- While `in_flight_ == true`, external user inputs are accepted into the input queue but must not start a new input-chain request.
- Tool-call reply requests do not affect `in_flight_`.

## Ordering / determinism

- Tool results are appended to history in completion order.
- Deterministic ordering is not required because tool results are matched by `tool_call_id`.

## Resolved details

- **No additional per-round reset on tool-call reply**: `StartLlmRequest(...)` does not reset state. Correctness relies on the invariant that no new LLM request is sent until all tool calls complete, therefore `pending_tool_call_count_ == 0` when a tool-call reply request is sent.

- **Tool result format**: OpenAI-compatible tool messages require `role=tool`, `tool_call_id`, and `content` as a string. The project does not enforce any specific `content` format.

- **Assistant text during tool-call rounds**: any streaming text deltas that occur in a tool-call round are not recorded into history; only `assistant(tool_calls=...)` and subsequent `tool` result messages are appended.

- **History ordering**:
  - `assistant(tool_calls=batch)` is appended once per `OnToolCalls(batch)` callback.
  - Tool results are appended in completion order.
  - Interleaving is allowed across multiple tool-call batches (e.g., tool results for a later batch may appear before results for an earlier batch) as long as `tool_call_id` alignment is preserved.
