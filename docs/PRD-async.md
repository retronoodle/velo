# PRD: Non-blocking waits for AI and git

GitHub issue #12 ("threads for waits").

## Summary

AI requests and git network operations block the whole editor until they finish. Make the long waits run in the background so the user can keep typing, scrolling and switching tabs. Use the existing fork + pipe pattern from `aiasync.c`, not threads.

## Goals

- Git push and pull (and the AI commit-message draft) in the Alt+G panel no longer freeze the editor.
- The AI chat no longer freezes the editor while a reply (and its tool round trips) is in progress.
- All buffer and undo changes stay on the main thread.
- Keep the plain `make` build with no new library dependencies.

## Non-goals

- Threads, locks, or any shared-memory concurrency. `editor`, undo and the other globals are not thread-safe.
- Making Explain and Ask AI (Alt+I) asynchronous. Both are modal, so there is little to gain, and Ask AI has a drift hazard (the selection moves if the user edits mid-request).
- Streaming tokens.
- A Windows implementation. Windows keeps the current blocking path, as autocomplete does today (`aiasync` reports unsupported).

## Design

### Why fork + pipe

`aiasync.c` already runs curl in a forked child, reads the reply from a pipe, and the main loop polls every 100 ms while busy. Because the child is a separate process there are no data races, and nothing outside the main thread touches `editor` or the undo log. The remaining bugs are logical (stale replies, cleanup), which are easy to reason about.

### A shared job helper

Today `aiasync.c` is AI-specific. Pull the generic part into a small job helper (`bgjob.c/.h`, name to be settled):

- `bgjob_start(argv)` forks and execs a command with stdout/stderr going to a pipe (no shell).
- `bgjob_poll()` does a non-blocking read into a buffer and reports running / finished (exit code) / failed.
- `bgjob_cancel()` kills the child, reaps it (`waitpid`) and removes any temp files.
- Every exit path (finish, cancel, panel close, tab close, program exit) reaps the child, so no zombies.
- POSIX only. On Windows `bgjob_start` reports unsupported and callers fall back to the blocking code.

`aiasync.c` is rebuilt on top of it so there is one implementation.

### Git panel (`gitmodal.c`)

- Push, pull and the commit-message draft start a job and put the panel into a busy state (spinner via `aiwait`, status text such as "Pushing...").
- While busy: the panel ignores other git actions, Esc cancels the job, and the panel stays drawable. Closing the panel cancels the job.
- When the job finishes, the existing result handling runs unchanged (the `--set-upstream` hint, the "remote is ahead" pull-then-push offer, merge conflict stop, error messages).
- Offers that open a prompt (set upstream, pull then push) still run on the main thread after the job ends.

### AI chat (`aichat.c`)

The blocking loop of up to `MAX_STEPS` round trips becomes a small state machine:

1. **Sending**: a request is in flight (a job).
2. **Tool**: the reply asked for tools. Run them on the main thread (they are quick and read-only, plus `fetch_url`, which stays a blocking curl GET for now), append the results, then go back to Sending.
3. **Done**: the reply has no tool calls. Build the proposal or plan exactly as today.

Rules:

- The main loop polls the job every 100 ms while the chat is busy. Typing in the editor works normally.
- Esc cancels the job (today Esc only cancels between steps). Ctrl+L, closing the chat and exiting also cancel it.
- A proposal stays bound to the tab and the file content hash, so edits made while waiting are caught at apply time as they are now.
- The log shows the spinner while busy. A second message cannot be sent until the turn ends.
- Tool exchanges are still not kept in the history between turns.

### Constraints

- All text edits still go through the `buffer.c` edit functions so undo records them.
- Nothing is sent in the background unless the user triggered the feature.
- Temp files (curl config, body) stay 0600 and are removed on every exit path, including cancel.
- Windows keeps working with the blocking code paths.

## Tasks

### Phase 1: Job helper

- [ ] 1.1 Read `docs/PRD-async.md`
- [ ] 1.2 Create `bgjob.c/.h` (start, poll, cancel, reap) and add them to `SOURCES`/`HEADERS` in the Makefile
- [ ] 1.3 Rebuild `aiasync.c` on top of the helper, keeping autocomplete behaviour unchanged
- [ ] 1.4 Unit tests for the pure parts (output buffering, status handling) in `tests/` and a `make test` entry

### Phase 2: Git panel

- [ ] 2.1 Add a busy state to the Alt+G panel, with spinner and Esc to cancel
- [ ] 2.2 Run push through a job, keeping the set-upstream and remote-ahead handling
- [ ] 2.3 Run pull (and the pull-then-push retry) through a job, keeping the conflict handling
- [ ] 2.4 Run the AI commit-message draft through a job
- [ ] 2.5 Cancel the job when the panel closes; fall back to blocking on Windows

### Phase 3: AI chat

- [ ] 3.1 Split the chat turn into a state machine (sending, tool, done) in `aichat.c`
- [ ] 3.2 Poll the request from the main loop while busy, and show the spinner
- [ ] 3.3 Esc, Ctrl+L, closing the chat and exit cancel the request and clean up temp files
- [ ] 3.4 Keep proposal and plan handling (hash binding, `^S`/`^E`/`^R`) unchanged
- [ ] 3.5 Fall back to the blocking loop on Windows

### Phase 4: Docs

- [ ] 4.1 Update `CLAUDE.md` (module list, `bgjob`, `aiasync`, git panel and chat notes) and the in-app help
