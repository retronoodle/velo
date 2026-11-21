# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project Overview

This is a simple text editor, **velo**, written in C. It runs in the terminal with a full-screen interface and supports basic editing operations.

## Build and Run

- **Build**: `make` (produces `velo.exe` on Windows)
- **Clean**: `make clean`
- **Run**: `./velo.exe [filename]`

## Architecture

### Module Layout

Sources live in `src/` (paths below are relative to it). Unit tests live in `tests/`.

- `editor.h`: shared types (`Line`, `Editor`), screen size, and the global `extern Editor editor`
- `term.c/.h`: frame buffer (`fb_*`, `fb_flush`), console/raw mode, `KEY_*` codes, `read_key()`
- `buffer.c/.h`: `Editor editor` definition, line storage, editing ops (`insert_char`, `backspace`, ...), `move_cursor`/`adjust_scroll`, `init_editor`
- `menu.c/.h`: menu tables (`menus[]`, items with label/hint/mnemonic/action), menu-mode keys (`menu_open_key`, `menu_handle_key`), and the `ACT_*` action codes
- `options.c/.h`: `options` (tab width, line endings, color scheme, line numbers, syntax, word wrap, and the AI settings `ai_provider`/`ai_model`/`ai_base_url`/`ai_key` saved in `~/.velo_config`), `options_ai_key()` (env var `ANTHROPIC_API_KEY`/`OPENAI_API_KEY` wins over the stored key), `options_cycle` (saves after each change), `options_load`/`options_save` (`~/.velo_config`, `key=value` lines; line endings are not saved, they are per file), `options_parse_line` (unit tests in `tests/test_options.c`), `options_label`, `options_base_sgr` (the scheme's SGR; use it instead of a plain `\x1b[0m`), `options_chrome_sgr(ChromeKind)` (bars, tabs, status line, borders, per scheme)
- `ai.c/.h`: pure AI helpers (JSON escape/unescape, reply lookup, request bodies, curl config, reply parsing, and the chat/tool-use message, body and tool-call helpers). No I/O; unit tests in `tests/test_ai.c` (`make test`)
- `aiask.c/.h`: `ai_ask()`, the blocking AI request. Runs curl via `popen` with the config and body in 0600 temp files (key stays off the command line). Settings come from Options > AI
- `aisetup.c/.h`: Options > AI setup (`ai_setup()`): prompts for provider, model, base URL and key. The key is shown masked / "set" / "from env" and never echoed
- `aiedit.c/.h`: Ask AI (Alt+I, Edit menu): prompts for an instruction, sends the selection (or the text around the cursor), and puts the reply in place of the selection (or at the cursor) as one undo step. Strips code fences
- `aiwait.c/.h`: animated spinner + elapsed seconds on the message row while an AI request runs (`aiwait_begin`/`aiwait_tick`/`aiwait_end`, draw-only). `ai_run_curl` (aiask.c) polls in 100 ms slices on POSIX and ticks it; autocomplete ticks it from `aicomplete_tick`. New AI waits should call `aiwait_begin` before the request and `aiwait_end` after
- `aiasync.c/.h`: non-blocking AI request for autocomplete (POSIX `fork` + curl to a pipe, `aiasync_start`/`aiasync_poll`/`aiasync_cancel`; Windows reports unsupported). Reuses `ai_write_temp` from aiask.c
- `aicomplete.c/.h`: AI autocomplete (Alt+Space, Edit > Complete): requests in the background (the main loop polls every 100 ms while busy), shows the reply as dim ghost text drawn from `refresh_screen`, Tab accepts it via `clip_paste_text` (one undo step), any other key or click dismisses and cancels. Context comes from `ai_span_text`/`ai_cap_context` (aiedit.c)
- `aiweb.c/.h`: pure helpers for the chat's `fetch_url` tool: `aiweb_url_ok` (http/https only, plain URL characters, no `user@`, no IPv6 literals, no localhost/`.local`/private or odd-form IPv4), `aiweb_url_in_text` (allow-list test: whole-URL match), `aiweb_html_to_text` (drops script/style/head/comments, decodes entities, block tags become line breaks), `aiweb_wrap_result` (untrusted-data header, `AIWEB_MAX_OUT` cap, `start=` paging note). Unit tests in `tests/test_aiweb.c` (`make test`)
- `aifetch.c/.h`: `aifetch_url()`, one blocking curl GET for `fetch_url` (URL goes in a 0600 curl config file, never the command line; 20 s, 2 MB, http/https only; non-text content types refused; redirects are followed by hand, up to 5, re-checking each hop with `aiweb_url_ok`, and the host is resolved first so every address must be public and curl is pinned to one with `--resolve`). Uses `ai_run_curl` from aiask.c so the spinner keeps moving. The chat only allows URLs that appear in something the user typed this chat (not a plan being run) or in an open tab the AI did not create (`fetch_allowed` in aichat.c), so a fetched page cannot steer the model to another URL
- `aitools.c/.h`: project tools for the AI chat (read-only `list_dir`, `read_file`, `grep`; plus write helpers `aitools_resolve_write`, `aitools_make_parents`, `aitools_load_text`, `aitools_replace_once` that never write a file themselves), sandboxed to the startup folder (`aitools_set_root()` at launch): no hidden files/folders, no `build`/`vendor`/`node_modules`, no `..` or symlink escapes, output capped (`AITOOLS_MAX_OUT`). Errors come back as "Error: ..." text for the model. POSIX only (Windows answers with an error). Unit tests in `tests/test_aitools.c` (`make test`)
- `sidebar.c/.h`: file tree sidebar (Alt+B, Edit > File Tree). A left pane (`sidebar_width()` is taken off `text_w()`; 22-32 columns, hidden when less than 40 columns would remain beside it and the chat) showing the startup folder (`sidebar_set_root()`). `[+]`/`[-]` click to expand/collapse, double-click or Enter opens a file via `open_path` (folders toggle), arrows/`+`/`-` navigate, `n`/`N` new file/folder (prompt, then the new file opens), `c`/`v` or Ctrl+C/Ctrl+V copy and paste a file or folder into the selected folder, `r` refresh, `h` hide/show `.git`, `node_modules`, `build`, `vendor` (everything shows by default, dot-files included), right-click opens a popup with New File, New Folder, Copy, Paste, Refresh. `sidebar_key` returns 0 for keys it does not own so Ctrl+Q/Ctrl+S/Ctrl+Z/F6 still reach the editor; only one of the sidebar and the chat has focus at a time (`aichat_unfocus`/`sidebar_blur`). Editor text starts at `text_x0()` (= `sidebar_width()`): use it for any screen-column math on the text area (drawing, cursor, mouse)
- `filetree.c/.h`: pure tree model for the sidebar: a flat list of visible rows, folders read lazily through a callback (`ft_expand`/`ft_collapse`/`ft_toggle`/`ft_reveal`/`ft_refresh`, folders first then case-insensitive names, capped by `FT_MAX_CHILDREN`/`FT_MAX_ROWS`). Unit tests in `tests/test_filetree.c` (`make test`)
- `fileops.c/.h`: sidebar file operations (`fileops_new_file`, `fileops_new_dir`, `fileops_copy` recursive without following symlinks, refuses a folder into itself, `fileops_unique_name` gives "name copy.ext"); never overwrite. Unit tests in `tests/test_fileops.c` (`make test`)
- `aichat.c/.h`: AI chat sidebar (Alt+J, Edit > AI Chat). A right-hand pane (`aichat_width()` is taken off `text_w()`; hidden below 80 columns) with session history, a multi-line input (`msgedit.c`) and wrapped log. Each message sends the active file (capped at `FILE_CAP`) plus the kept history; the model can call the tools (and `open_tabs`, `fetch_url`) over up to `MAX_STEPS` blocking round trips (Esc cancels between steps). A reply with one fenced block becomes a proposal bound to that tab and file content (hash): `^S` applies it as one undo step, changing only the differing line hunks (`linediff`, bottom to top; falls back to select all + `clip_paste_text` if the result doesn't match), `^E` shows a `diff -u` in `view_diff`, `^R` drops it. Ctrl+L starts a new chat (clears the log, history and proposal). Tool exchanges are not kept in the history between turns. The model can also call `write_file` (new file) and `edit_file` (exact one-match replace, for files not open in a tab); `queue_write` only validates and queues them, and after the turn `open_pending` opens each as an unsaved tab (`open_path` then `apply_hunks`, so Ctrl+Z reverts to the disk text and nothing is saved until Ctrl+S), then switches back to the proposal's tab Keys: `aichat_key` returns 0 for keys it does not own so Ctrl+Q/Ctrl+Z/F6 still reach the editor
- Plan mode (Alt+P, Edit > Plan Mode; in `aichat.c`): the chat uses `PLAN_SYSTEM` and read-only tools only, `fetch_url` included (`ai_chat_tools_json_ro`; `ai_ask_chat` `use_tools == 2`; `run_tool` also refuses `write_file`/`edit_file`), and the reply becomes `plan_text` (in memory) instead of a proposal. `^S` approves: `plans_save` writes it to `~/.velo_plans/<stamp>-<slug>.md` (newest 20 kept), then asks to run it here or in a new chat (`new_chat()` + the plan as the first message; the log shows a short label, the history keeps the full text). `^E` views it, `^R` drops it, typing feedback revises it. A pending plan and a pending proposal never coexist (`drop_proposal` clears both). Header shows `[PLAN]`; Ctrl+L resets it
- `plans.c/.h`: saved plans: `plans_slug()` (pure) and `plans_save()` (`~/.velo_plans`, `O_EXCL`, prune to `PLANS_KEEP`; POSIX only). Unit tests in `tests/test_plans.c` (`make test`)
- `chatmd.c/.h`: pure helpers for the chat pane: `chatmd_row()` word-aware wrapping (breaks at spaces, splits only words wider than the pane) and `chatmd_render()` (Markdown to plain text plus a `CM_*` style byte per byte: headings, bold, italic, code, fences, bullets, quotes, rules, links; unterminated markers stay literal). Only AI replies are rendered; history sent to the model stays raw. Unit tests in `tests/test_chatmd.c` (`make test`)
- `aiinstr.c/.h`: project instruction files read from the startup folder on every request and appended to the system prompt: `AGENTS.md` (chat, 16 KB cap) and `SMALL_AGENTS.md` (autocomplete, Explain, Ask AI, 4 KB cap; not the commit-message AI). `aiinstr_join` is pure; unit tests in `tests/test_aiinstr.c` (`make test`)
- `aiexplain.c/.h`: Explain (Edit menu): sends the selection and shows the reply in `view_text`
- `viewer.c/.h`: `view_text()`, a read-only scrollable modal for long text (Esc/Enter/q closes, Up/Down scroll); `view_diff()` adds diff tints (add/del/hunk) and syntax colors for the code, per scheme
- `gitinfo.c/.h`: cwd (home shortened to `~`) and git branch for the status line, read from `.git` files (no git process); cached, refreshed every 2 s
- `gitmodal.c/.h`: Alt+G git panel: status of the cwd's repo, Space stages/unstages the selected file, Enter or a left click shows its diff vs HEAD in `view_diff` (`do_file_diff`, capped at `VIEW_DIFF_MAX`), wheel moves the selection (S/U stage or unstage all), commit (staged files only; offers to stage all when none are), push, B branch list (switch / + new branch), M merge a branch, A abort merge, D drafts a commit message with the AI (a subject line plus bullets) into the commit message box at the bottom of the panel, which has focus: typing edits it (`msgedit`), Ctrl+S commits it, Esc hides it and keeps the draft (E reopens it; C commits it after asking). The input is git's `--stat` plus untracked file names, then the diff capped per file (`diffcap_trim`, budget `DIFF_MAX`). Leaving the panel discards the draft. Runs git as a child process (`popen`), unlike gitinfo. Push offers git's `--set-upstream` hint (`gitpush.c`) and runs it through `run_argv` (no shell). When the remote is ahead (`[rejected]` ... `(fetch first)`), it offers a `git pull --no-rebase --no-edit` and one retry of the push (`offer_pull_then_push`); a conflicting pull stops with the merge open. The I key runs `git init` when the cwd isn't a repo. N (shown when there's no `origin`) creates a GitHub repo via `gh repo create --source=. --remote=origin --push` (private by default; type `public` for public), POSIX only.
- `brace.c/.h`: pure bracket helpers for auto-close (`(`, `[`, `{`, `"`, `'`: typing a closer steps over it, Backspace on an empty pair removes both, Enter in `{}` splits the pair), and the matching-bracket highlight (`brace_match_for_cursor`, drawn by `draw_row` in ui.c). Options > Brace match (`options.brace`). Unit tests in `tests/test_brace.c` (`make test`)
- `msgedit.c/.h`: pure multi-line text buffer for the commit message box (insert, paste cleanup, backspace/delete, Up/Down keeping a goal column, line and cursor lookup). Unit tests in `tests/test_msgedit.c` (`make test`)
- `gitpush.c/.h`: pure parsers for git's push failures: the "no upstream" hint (`git push --set-upstream <remote> <branch>`, plain-token checks) and `gitpush_is_rejected_behind()` for a remote that is ahead. Unit tests in `tests/test_gitpush.c` (`make test`)
- `linediff.c/.h`: pure line diff (`linediff()`: common head/tail trim, then LCS over the middle, one fallback hunk above ~4M cells) used by chat apply. Unit tests in `tests/test_linediff.c` (`make test`)
- `diffcap.c/.h`: pure text shrinking for AI prompts: `diffcap_trim()` (per-file share of a diff budget, every header kept) and `diffcap_head()` (whole lines within a budget). Unit tests in `tests/test_diffcap.c` (`make test`)
- `ui.c/.h`: status line, menu bar, dropdown, editor area, `refresh_screen`, `show_message`, `prompt_line`, `show_help`
- `tabs.c/.h`: open documents as tabs. The active document is the global `editor` (plus undo.c's active history); other tabs hold their own copy in a slot. `tab_switch()` moves data between them. Tab bar drawing and hit-testing live here too (`tab_bar_range`, `tab_bar_hit`)
- `files.c/.h`: save/save-as/new tab/open (`open_path`)/close tab/exit (asks about each modified tab), the folder browser (`browse`, shared by Open and Save As) and the recent files list (`do_open_recent`)
- `quickopen.c/.h`: Quick Open (Ctrl+P, File > Quick Open...): scans every file under the startup folder (captured by `quickopen_set_root()` at launch, not the cwd, which the Open browser moves) once (skips hidden folders, node_modules, build, etc.; capped at 20000 files), then filters as you type. Recent files sort first when the query is empty
- `fuzzy.c/.h`: pure `fuzzy_score()` for Quick Open (case-insensitive subsequence, bonuses for file-name and word-start matches). Unit tests in `tests/test_fuzzy.c` (`make test`)
- `recent.c/.h`: last 10 files opened or saved (full paths), kept in `~/.velo_recent` (falls back to the old `~/.edit_recent` on first run) (`USERPROFILE` or `HOME`)
- `syntax.c/.h`: keyword/comment/string coloring. One scanner per language (chosen by file extension, or per document by `editor.syn_override` via Options > Language; `syntax_lang_effective`), each line scanned with the parser state carried from the line above. `draw_editor_area()` (ui.c) rescans from `SYN_LOOKBACK` (1000) lines above the screen each redraw to get that state. Options > Syntax turns it off. Unit tests in `tests/test_syntax.c` (`make test`)
- Word wrap (Options > Word wrap, `options.wrap`): soft-wraps lines at the text width. Wrap math is pure and lives in `utf8.c` (`utf8_wrap_rows`, `utf8_wrap_row`, `utf8_wrap_cursor`, tested in `tests/test_utf8.c`). `ui.c` draws each file line as several rows via `draw_row(file_line, show_num, from, to, col0, scroll, ...)`; `buffer.c` has `line_rows()` and `cursor_set_wrapped()` for Up/Down and clicks; `scroll_x` stays 0 while wrap is on
- `clipboard.c/.h`: selection (Shift+arrows, Alt+Shift+arrows for a rectangular block when word wrap is off, Ctrl+A select all), internal clipboard, cut/copy/paste (Ctrl+X/C/V). Copy also sets the system clipboard: OSC 52 on POSIX (skipped above 100 KB), the Windows clipboard on Windows. Ctrl+V reads the system clipboard first (`wl-paste`, `xclip`, `xsel` on Linux) and falls back to the internal one. Terminal pastes (bracketed paste, `KEY_PASTE`) go through `clip_paste_text()`
- `clipcodec.c/.h`: pure helpers, base64 for OSC 52 and paste cleanup (CRLF to LF, control bytes dropped). Unit tests in `tests/test_clipcodec.c` (`make test`)
- `undo.c/.h`: undo/redo operation log (`undo_record_*` called from buffer.c, `undo_undo`/`undo_redo`, Ctrl+Z/Ctrl+Y)
- `search.c/.h`: Find, Find Next, Replace, Go to Line (Ctrl+F, F3, Ctrl+R, Ctrl+G; Search menu)
- `utf8.c/.h`: UTF-8 decode/encode, character boundaries, display width (`cp_width`, `utf8_col`, `utf8_byte_at_col`). Columns are tab-aware: `utf8_cell_w(cp, col)` gives a tab's width to the next stop (`utf8_set_tab_width`, called from options) and 2 for a C0 control shown as `^X`. Use it for any column math, not `cp_width`. `tests/test_utf8.c` holds its unit tests (`make test`)
- `watch.c/.h`: detects the active file changing on disk (mtime/size, polled on the 1 s idle tick, no threads) and offers to reload. `watch_record()` runs after a load or save so our own writes never prompt
- `recovery.c/.h`: crash recovery. `recovery_tick()` (main loop) writes `<file>.recover` for every modified tab every 30 s; `recovery_discard_name()` runs after save, on discard and on tab close; `recovery_check_on_startup()` offers to restore the active tab
- `mouse.c/.h`: mouse clicks, drag selection, double-click word select, wheel scrolling, right-click (opens the Edit menu) and menu-bar clicks (`mouse_handle`). `term_ticks_ms()` gives the clock for double-click timing. term.c reads the events (`KEY_MOUSE`, `term_get_mouse`); `read_key()` skips them, so only the main loop sees mouse input
- `velo.c`: `main`, main loop, `run_action()` (runs a menu action or key command)

New features should go in their own module (e.g. `search.c`) rather than growing `velo.c`.

AI: one global AI setting (Options > AI setup) is shared by every AI feature. Requests block the UI behind a "Thinking..." message, and the `curl` timeout bounds the wait. Nothing is sent unless the user triggers a feature. AI edits must go through the `buffer.c` edit functions so undo records them.

Edit functions in buffer.c (`insert_char`, `backspace`, ...) mutate text via `raw_*` helpers and then record the change for undo. Undo/redo use the `raw_*` helpers directly so they are not recorded again. Any code that changes lines outside these functions (e.g. file load) must call `undo_clear()`.

### Core Structure

The editor uses a single `Editor` struct (editor.h) that maintains:
- Growable array of `Line` structs (each holds a heap-allocated, NUL-terminated `text` plus `len`/`cap`); no fixed line or length limit
- Cursor position (cursor_x, cursor_y)
- Scroll offset (scroll_x, scroll_y)
- Insert/overwrite mode flag
- Modified flag for tracking unsaved changes

### Platform Abstraction

The code uses conditional compilation (#ifdef _WIN32) to handle platform differences:
- **Windows**: Uses conio.h for _getch(), Windows console API for cursor/screen control
- **POSIX**: Uses termios for raw mode, ANSI escape sequences for cursor/screen control

Key abstracted functions:
- `read_key()`: Normalizes keyboard input to unified KEY_* codes (term.c)
- `gotoxy()`, `clear_screen()`, `set_cursor_visibility()`: Platform-specific screen control

### Keyboard Handling

The editor normalizes all keyboard input through `read_key()` which returns unified key codes:
- Standard keys (KEY_UP, KEY_DOWN, KEY_LEFT, KEY_RIGHT, etc.)
- Function keys (KEY_F1-F10)
- Special keys (KEY_ESC, KEY_ENTER, KEY_BACKSPACE, KEY_TAB, KEY_INSERT, KEY_DELETE)

This fixes cross-platform issues like:
- Backspace sending DEL (127) vs BS (8) on different terminals
- Function keys sending escape sequences that could trigger exit

### Screen Layout

Screen size comes from the console and follows window resizes (`term_update_size()` sets `screen_w`/`screen_h`; 80x25 is the fallback, 40x10 the minimum). Rows are relative to `screen_h`:
- Line 0: Menu bar showing "File Edit Search Options" (all four are working dropdowns)
- Line 1 (`TAB_ROW`): Tab bar, one label per open file; the active tab is reverse video, `*` marks unsaved changes
- Lines `TEXT_TOP` (2) to screen_h-3: Editable text area (`TEXT_ROWS` rows; use these constants, not literal row numbers)
- Line screen_h-2: Message and prompt row (blank when drawing; messages are printed over it)
- Line screen_h-1: Status line with function key hints, cursor position, mode (INS/OVR), modified flag (*), filename

### Main Loop

The main loop in `main()` (velo.c) reads a key. Menu activation and menu-mode keys go to `menu.c`, which returns an `ACT_*` code; `run_action()` in velo.c runs it. Otherwise the key goes to the editing switch. Menu items and key shortcuts share the same functions.

## Development Notes

- Line storage grows on demand (`line_reserve`, `lines_reserve`, `insert_line_at`, `delete_line_at` in buffer.c); `reset_document()` clears the buffer
- Build is `make` (compiles all `.c` files listed in the Makefile). Add new `src/*.c` files to `SOURCES` and their headers to `HEADERS` (both use `$(SRC)/`)
- The file browser still caps listings at MAX_DIR_ENTRIES=512
- Clipboard: copy goes to the system clipboard too (see `clipboard.c`). Bracketed paste is on while velo runs (`enable_raw_mode`), so Ctrl+Shift+V in the terminal pastes into velo. Windows has no bracketed paste, so terminal pastes arrive as typed keys there
- Tab key inserts `options.tab_width` spaces (2, 4 or 8; default 4)
- Files are saved with LF by default. Opening a file with CRLF switches saving to CRLF; the Options menu can change it. Files are written and read in binary mode, so the line ending is controlled by `options.crlf`
- Options are saved to `~/.velo_config` (`USERPROFILE` or `HOME`) on every change and loaded at startup, before files open. Missing or bad lines fall back to the defaults
- The main loop uses `read_key_wait(ms)` (1 s timeout) so backups run while idle. Only the main loop should use it; prompts and browsers use `read_key()`, which blocks
- `files.c:load_stream()` reads a stream into the buffer. Recovery uses it, so keep it free of filename or modified-flag changes
- Per-document state (lines, cursor, filename, modified, selection, undo history, CRLF) must move with its tab. Add new per-document fields to `Editor` or `undo.c`'s `UndoState`, and check `tabs.c` (`stash_active`/`load_active`) still carries them. Global state stays global (clipboard, search text, options)
- The tab limit is `MAX_TABS` (16). Tabs are closed with Ctrl+W; F6/F7 move between them
