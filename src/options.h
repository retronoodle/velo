/* User options set from the Options menu. Saved to ~/.velo_config (line endings excepted: per file). */
#ifndef OPTIONS_H
#define OPTIONS_H

#include "gitstat.h"

typedef struct {
    int tab_width;   /* spaces inserted by Tab: 2, 4 or 8 */
    int crlf;        /* 1 = save with CRLF line endings, 0 = LF */
    int scheme;      /* index into the color schemes: 0 Default, 1 Blue, 2 Light, 3 Slate */
    int line_numbers; /* 1 = show line numbers in a left gutter, 0 = off */
    int syntax;       /* 1 = color keywords, comments and strings, 0 = plain text */
    int wrap;         /* 1 = soft-wrap long lines at the window edge, 0 = scroll sideways */
    int brace;        /* 1 = auto-close brackets and quotes, highlight the matching bracket */
    char ai_provider[16];  /* "anthropic" or "openai", empty = not set */
    char ai_model[128];    /* model name sent with AI requests */
    char ai_base_url[256]; /* base URL for the openai provider */
    char ai_key[512];      /* stored API key (an env var takes precedence) */
} Options;

extern Options options;

/* API key for the configured AI provider: the env var (ANTHROPIC_API_KEY or
   OPENAI_API_KEY) if set, else the stored ai_key, else "". Never log it. */
const char *options_ai_key(void);

/* Advances the option for an ACT_TAB_WIDTH / ACT_LINE_ENDS / ACT_COLOR_SCHEME / ACT_LINE_NUMBERS / ACT_SYNTAX / ACT_WRAP / ACT_BRACE action. */
void options_cycle(int action);

/* Reads ~/.velo_config. Missing or unreadable keeps the defaults. */
void options_load(void);

/* Writes the saved options to ~/.velo_config. Failure is silent. */
void options_save(void);

/* Applies one "key=value" line (comments, blank lines and bad values are ignored). Returns 1 if it was applied. */
int options_parse_line(const char *line);

/* Writes the item text for an Options menu action, e.g. "Tab width: 4". */
void options_label(int action, char *buf, int size);

/* SGR sequence that resets attributes and sets the current color scheme. Use it instead of a plain reset. */
const char *options_base_sgr(void);

/* Colors for the screen chrome (bars, tabs, status line, borders), per scheme. */
typedef enum {
    CH_BAR,       /* menu bar */
    CH_BAR_SEL,   /* open menu title */
    CH_TAB_BAR,   /* tab bar background and inactive tabs */
    CH_TAB_ACT,   /* active tab */
    CH_STATUS,    /* status line */
    CH_BRANCH,    /* git branch on the status line */
    CH_WARN,      /* unsaved-changes marker (fg only, keeps the background) */
    CH_DIM,       /* borders, line-number gutter, hints */
    CH_DROP_SEL,  /* highlighted dropdown item */
    CH_COUNT
} ChromeKind;

/* SGR for a chrome element in the current scheme. Each starts with a reset,
   except CH_WARN, which only changes the foreground. */
const char *options_chrome_sgr(ChromeKind kind);

/* Foreground SGR (no reset) for a file tree entry's git status; "" for GK_NONE. */
const char *options_git_sgr(GitKind kind);

#endif
