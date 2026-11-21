/* One blocking AI request using the global AI settings (Options > AI). See aiask.c. */
#ifndef AIASK_H
#define AIASK_H

#include <stddef.h>

/* Sends one request: system prompt (may be NULL or empty), user input, using the
   configured provider, model, base URL and key. On success returns 0 and sets *out
   to a malloc'd reply (caller frees). On failure returns -1, sets *out to NULL and
   fills err (size errsize) with a short message. Blocks until curl finishes (60 s cap). */
int ai_ask(const char *system, const char *input, char **out, char *err, int errsize);

/* One chat request: history msgs[0..n) (provider-format message JSON, see ai.h), with the
   read-only tools when use_tools is set. On success returns 0 and sets *raw to the raw
   response body (malloc'd) for ai_chat_parse_calls / ai_parse_reply. On failure returns -1,
   *raw is NULL and err holds a short message. Blocks until curl finishes (60 s cap). */
/* use_tools: 0 none, 1 all tools, 2 read-only tools (plan mode). */
int ai_ask_chat(const char *system, const char *const *msgs, int n, int use_tools,
                char **raw, char *err, int errsize);

/* Writes data to a new 0600 temp file and sets path (size n). Returns 1 on success.
   The caller removes the file. */
int ai_write_temp(char *path, size_t n, const char *data);

/* Runs a curl command line and reads its output into *buf (malloc'd, NUL-terminated), ticking
   the aiwait spinner every 100 ms on POSIX. Returns curl's exit status or -1. *too_big is set
   when the output passed max_bytes (reading stops there). */
int ai_run_curl(const char *cmd, char **buf, size_t *len, int *too_big, size_t max_bytes);

#endif
