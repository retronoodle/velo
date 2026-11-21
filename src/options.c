/* User options: tab width, line endings, color scheme and the saved config file. */
#ifndef _WIN32
#define _POSIX_C_SOURCE 200809L   /* fdopen, fchmod-era POSIX calls under -std=c99 */
#endif
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifndef _WIN32
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

#include "menu.h"
#include "options.h"
#include "recent.h"   /* home_file_path */
#include "utf8.h"     /* tab stops for display columns */

#define CONFIG_NAME ".velo_config"

Options options = {4, 0, 0, 0, 1, 0, 1, "", "", "", ""};

static const char *scheme_names[] = {"Default", "Blue", "Light", "Slate"};

/* Each starts with a reset so the scheme never leaks into other attributes. */
static const char *scheme_sgr[] = {
    "\x1b[0m",          /* Default: terminal colors */
    "\x1b[0;37;44m",    /* Blue: white on blue */
    "\x1b[0;30;47m",    /* Light: black on white */
    "\x1b[0;38;5;253;48;5;236m",  /* Slate: light grey on dark slate */
};

void options_cycle(int action) {
    switch (action) {
        case ACT_TAB_WIDTH:
            options.tab_width = options.tab_width == 2 ? 4 : options.tab_width == 4 ? 8 : 2;
            utf8_set_tab_width(options.tab_width);
            break;
        case ACT_LINE_ENDS:
            options.crlf = !options.crlf;
            break;
        case ACT_COLOR_SCHEME:
            options.scheme = (options.scheme + 1) % 4;
            break;
        case ACT_LINE_NUMBERS:
            options.line_numbers = !options.line_numbers;
            break;
        case ACT_SYNTAX:
            options.syntax = !options.syntax;
            break;
        case ACT_WRAP:
            options.wrap = !options.wrap;
            break;
        case ACT_BRACE:
            options.brace = !options.brace;
            break;
    }
    options_save();
}

/* Parses a whole number. Returns 1 and sets *out only if the text is all digits. */
static int parse_number(const char *s, int *out) {
    char *end;
    long n = strtol(s, &end, 10);
    if (end == s || *end != '\0') return 0;
    *out = (int)n;
    return 1;
}

/* Copies a string setting. Returns 0 (and leaves dst alone) if it does not fit. */
static int set_text(char *dst, size_t size, const char *val) {
    if (strlen(val) >= size) return 0;
    strcpy(dst, val);
    return 1;
}

/* Sets one option from a key and its value. Returns 1 if the key is known and the value is valid. */
static int apply_option(const char *key, const char *val) {
    int n;
    if (strcmp(key, "ai_provider") == 0) {
        if (strcmp(val, "anthropic") != 0 && strcmp(val, "openai") != 0) return 0;
        return set_text(options.ai_provider, sizeof(options.ai_provider), val);
    }
    if (strcmp(key, "ai_model") == 0)
        return set_text(options.ai_model, sizeof(options.ai_model), val);
    if (strcmp(key, "ai_base_url") == 0)
        return set_text(options.ai_base_url, sizeof(options.ai_base_url), val);
    if (strcmp(key, "ai_key") == 0)
        return set_text(options.ai_key, sizeof(options.ai_key), val);

    if (!parse_number(val, &n)) return 0;
    if (strcmp(key, "tab_width") == 0) {
        if (n != 2 && n != 4 && n != 8) return 0;
        options.tab_width = n;
        utf8_set_tab_width(n);
    } else if (strcmp(key, "scheme") == 0) {
        if (n < 0 || n > 3) return 0;
        options.scheme = n;
    } else if (strcmp(key, "line_numbers") == 0) {
        if (n != 0 && n != 1) return 0;
        options.line_numbers = n;
    } else if (strcmp(key, "syntax") == 0) {
        if (n != 0 && n != 1) return 0;
        options.syntax = n;
    } else if (strcmp(key, "wrap") == 0) {
        if (n != 0 && n != 1) return 0;
        options.wrap = n;
    } else if (strcmp(key, "brace") == 0) {
        if (n != 0 && n != 1) return 0;
        options.brace = n;
    } else {
        return 0;
    }
    return 1;
}

static int is_blank(char c) {
    return c == ' ' || c == '\t' || c == '\r' || c == '\n';
}

int options_parse_line(const char *line) {
    while (is_blank(*line)) line++;
    if (*line == '\0' || *line == '#') return 0;

    const char *eq = strchr(line, '=');
    if (!eq) return 0;

    char key[32], val[sizeof(options.ai_key)];   /* as big as the largest text setting, so a long API key is not cut */
    size_t kl = (size_t)(eq - line);
    if (kl >= sizeof(key)) return 0;
    memcpy(key, line, kl);
    key[kl] = '\0';
    while (kl > 0 && is_blank(key[kl - 1])) key[--kl] = '\0';

    const char *v = eq + 1;
    while (is_blank(*v)) v++;
    if (strlen(v) >= sizeof(val)) return 0;   /* too long: reject rather than keep a cut-off value */
    strcpy(val, v);
    size_t vl = strlen(val);
    while (vl > 0 && is_blank(val[vl - 1])) val[--vl] = '\0';

    return apply_option(key, val);
}

void options_load(void) {
    char path[512];
    if (!home_file_path(CONFIG_NAME, path, sizeof(path))) return;
    FILE *f = fopen(path, "rb");
    if (!f) return;
    char line[1024];
    while (fgets(line, sizeof(line), f)) {
        options_parse_line(line);
    }
    fclose(f);
}

const char *options_ai_key(void) {
    const char *env = NULL;
    /* A custom base URL is not api.openai.com: an env key meant for OpenAI must not go there,
       so a stored key wins (the env var is still the fallback when none is stored). */
    if (strcmp(options.ai_provider, "openai") == 0 && options.ai_base_url[0] && options.ai_key[0])
        return options.ai_key;
    if (strcmp(options.ai_provider, "anthropic") == 0) env = getenv("ANTHROPIC_API_KEY");
    else if (strcmp(options.ai_provider, "openai") == 0) env = getenv("OPENAI_API_KEY");
    if (env && *env) return env;
    return options.ai_key;
}

void options_save(void) {
    char path[512];
    if (!home_file_path(CONFIG_NAME, path, sizeof(path))) return;
    FILE *f = NULL;
#ifndef _WIN32
    /* The file may hold an API key: create it 0600 so no one else can read it. */
    int fd = open(path, O_WRONLY | O_CREAT | O_TRUNC, S_IRUSR | S_IWUSR);
    if (fd < 0) return;
    f = fdopen(fd, "wb");
    if (!f) { close(fd); return; }
#else
    f = fopen(path, "wb");
    if (!f) return;
#endif
    fprintf(f, "tab_width=%d\n", options.tab_width);
    fprintf(f, "scheme=%d\n", options.scheme);
    fprintf(f, "line_numbers=%d\n", options.line_numbers);
    fprintf(f, "syntax=%d\n", options.syntax);
    fprintf(f, "wrap=%d\n", options.wrap);
    fprintf(f, "brace=%d\n", options.brace);
    if (options.ai_provider[0]) fprintf(f, "ai_provider=%s\n", options.ai_provider);
    if (options.ai_model[0]) fprintf(f, "ai_model=%s\n", options.ai_model);
    if (options.ai_base_url[0]) fprintf(f, "ai_base_url=%s\n", options.ai_base_url);
    if (options.ai_key[0]) fprintf(f, "ai_key=%s\n", options.ai_key);
    fclose(f);
}

void options_label(int action, char *buf, int size) {
    switch (action) {
        case ACT_TAB_WIDTH:
            snprintf(buf, size, "Tab width: %d", options.tab_width);
            break;
        case ACT_LINE_ENDS:
            snprintf(buf, size, "Line endings: %s", options.crlf ? "CRLF" : "LF");
            break;
        case ACT_COLOR_SCHEME:
            snprintf(buf, size, "Colors: %s", scheme_names[options.scheme]);
            break;
        case ACT_LINE_NUMBERS:
            snprintf(buf, size, "Line numbers: %s", options.line_numbers ? "On" : "Off");
            break;
        case ACT_SYNTAX:
            snprintf(buf, size, "Syntax: %s", options.syntax ? "On" : "Off");
            break;
        case ACT_WRAP:
            snprintf(buf, size, "Word wrap: %s", options.wrap ? "On" : "Off");
            break;
        case ACT_BRACE:
            snprintf(buf, size, "Brace match: %s", options.brace ? "On" : "Off");
            break;
        default:
            if (size > 0) buf[0] = '\0';
            break;
    }
}

const char *options_base_sgr(void) {
    return scheme_sgr[options.scheme];
}

/* Chrome colors, indexed [scheme][ChromeKind]; order matches the enum. */
static const char *chrome_sgr[4][CH_COUNT] = {
    /* Default: terminal colors, reverse video for bars */
    {
        "\x1b[0;7m",        /* CH_BAR */
        "\x1b[0;1m",        /* CH_BAR_SEL */
        "\x1b[0m",          /* CH_TAB_BAR */
        "\x1b[0;7m",        /* CH_TAB_ACT */
        "\x1b[0;7m",        /* CH_STATUS */
        "\x1b[0;7;1m",      /* CH_BRANCH */
        "\x1b[1;33m",       /* CH_WARN */
        "\x1b[0;2m",        /* CH_DIM */
        "\x1b[0;7m",        /* CH_DROP_SEL */
    },
    /* Blue: cyan bars on blue */
    {
        "\x1b[0;30;46m",    /* CH_BAR */
        "\x1b[0;1;37;44m",  /* CH_BAR_SEL */
        "\x1b[0;36;44m",    /* CH_TAB_BAR */
        "\x1b[0;1;30;46m",  /* CH_TAB_ACT */
        "\x1b[0;30;46m",    /* CH_STATUS */
        "\x1b[0;1;34;46m",  /* CH_BRANCH */
        "\x1b[1;31m",       /* CH_WARN */
        "\x1b[0;36;44m",    /* CH_DIM */
        "\x1b[0;30;46m",    /* CH_DROP_SEL */
    },
    /* Light: blue bars on white */
    {
        "\x1b[0;97;44m",    /* CH_BAR */
        "\x1b[0;1;30;47m",  /* CH_BAR_SEL */
        "\x1b[0;30;100m",   /* CH_TAB_BAR */
        "\x1b[0;1;97;44m",  /* CH_TAB_ACT */
        "\x1b[0;97;44m",    /* CH_STATUS */
        "\x1b[0;1;93;44m",  /* CH_BRANCH */
        "\x1b[1;31m",       /* CH_WARN */
        "\x1b[0;90;47m",    /* CH_DIM */
        "\x1b[0;97;44m",    /* CH_DROP_SEL */
    },
    /* Slate: light grey on dark slate, blue accents */
    {
        "\x1b[0;38;5;253;48;5;238m",        /* CH_BAR */
        "\x1b[0;1;38;5;255;48;5;24m",       /* CH_BAR_SEL */
        "\x1b[0;38;5;250;48;5;235m",        /* CH_TAB_BAR */
        "\x1b[0;1;38;5;255;48;5;24m",       /* CH_TAB_ACT */
        "\x1b[0;38;5;253;48;5;238m",        /* CH_STATUS */
        "\x1b[0;1;38;5;75;48;5;238m",       /* CH_BRANCH */
        "\x1b[1;38;5;215m",                 /* CH_WARN */
        "\x1b[0;38;5;245;48;5;236m",        /* CH_DIM */
        "\x1b[0;38;5;253;48;5;24m",         /* CH_DROP_SEL */
    },
};

const char *options_chrome_sgr(ChromeKind kind) {
    return chrome_sgr[options.scheme][kind];
}

/* Git status colors for the file tree, indexed [scheme][GitKind]. Foreground only (no reset),
   so they combine with the row's bold and base colors. Light needs darker shades. */
static const char *git_sgr[4][GK_COUNT] = {
    /* Default */
    {"", "\x1b[32m", "\x1b[32m", "\x1b[36m", "\x1b[31m", "\x1b[33m", "\x1b[1;31m"},
    /* Blue */
    {"", "\x1b[92m", "\x1b[92m", "\x1b[96m", "\x1b[91m", "\x1b[93m", "\x1b[1;91m"},
    /* Light */
    {"", "\x1b[32m", "\x1b[32m", "\x1b[36m", "\x1b[31m", "\x1b[38;5;130m", "\x1b[1;31m"},
    /* Slate */
    {"", "\x1b[38;5;108m", "\x1b[38;5;108m", "\x1b[38;5;73m", "\x1b[38;5;167m",
     "\x1b[38;5;179m", "\x1b[1;38;5;203m"},
};

const char *options_git_sgr(GitKind kind) {
    return git_sgr[options.scheme][kind];
}
