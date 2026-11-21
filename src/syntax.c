/* Syntax highlighting: hand-written scanners. Each line is scanned on its own,
   given the parser state left by the line before it. No regex, no dependencies. */
#include <ctype.h>
#include <string.h>

#include "syntax.h"

#define UC(x) ((unsigned char)(x))

static const char *KW_C =
    "auto break case char const continue default do double else enum extern float for goto if inline "
    "int long register restrict return short signed sizeof static struct switch typedef union unsigned "
    "void volatile while bool class namespace template typename public private protected new delete "
    "this nullptr true false try catch throw using virtual override";

static const char *KW_JS =
    "break case catch class const continue debugger default delete do else export extends finally for "
    "function if import in instanceof let new return super switch this throw try typeof var void while "
    "with yield async await of null true false undefined interface type enum implements private public "
    "protected readonly as from";

static const char *KW_PHP =
    "abstract and array as break callable case catch class clone const continue declare default die do "
    "echo else elseif empty enddeclare endfor endforeach endif endswitch endwhile eval exit extends final "
    "finally fn for foreach function global goto if implements include include_once instanceof insteadof "
    "interface isset list match namespace new or print private protected public readonly require "
    "require_once return static switch throw trait try unset use var while xor yield null true false "
    "self parent";

static const char *KW_PY =
    "False None True and as assert async await break class continue def del elif else except finally "
    "for from global if import in is lambda nonlocal not or pass raise return try while with yield";

static const char *KW_JSON = "true false null";

static const char *KW_SH =
    "if then else elif fi for while until do done case esac in function select time return exit break "
    "continue local export readonly declare unset shift source alias eval exec trap set test true false";

/* Sets kinds[from..to) to kind. */
static void mark(unsigned char *k, int from, int to, int kind) {
    for (int m = from; m < to; m++) k[m] = (unsigned char)kind;
}

/* Is w (n bytes) one of the space-separated words in list? */
static int is_word_in(const char *list, const char *w, int n) {
    const char *p = list;
    while (*p) {
        const char *s = p;
        while (*p && *p != ' ') p++;
        if (p - s == n && memcmp(s, w, n) == 0) return 1;
        if (*p) p++;
    }
    return 0;
}

static int is_ident(char c) {
    return isalnum(UC(c)) || c == '_';
}

/* Block comment: from is where the token is marked, body is where the search for "*\/" starts.
   Returns the end index and sets *st. */
static int mark_block(const char *t, int len, int from, int body, unsigned char *k, int *st) {
    int j = body, closed = 0;
    while (j + 1 < len) {
        if (t[j] == '*' && t[j + 1] == '/') { j += 2; closed = 1; break; }
        j++;
    }
    if (!closed) j = len;
    mark(k, from, j, TK_COMMENT);
    *st = closed ? SYN_ST_NONE : SYN_ST_BLOCK_COMMENT;
    return j;
}

/* Quoted string. The quote is at from, the body starts at body. A string
   still open at end of line stays open only if multiline (JS template). */
static int mark_str(const char *t, int len, int from, int body, char q, int multiline,
                    unsigned char *k, int *st) {
    int j = body, closed = 0;
    while (j < len) {
        if (t[j] == '\\') { j += 2; continue; }
        if (t[j] == q) { j++; closed = 1; break; }
        j++;
    }
    if (j > len) j = len;
    mark(k, from, j, TK_STRING);
    *st = (!closed && multiline) ? SYN_ST_TEMPLATE : SYN_ST_NONE;
    return j;
}

/* Python triple-quoted string. open_state is the state if it is still open at end of line. */
static int mark_triple(const char *t, int len, int from, int body, char q,
                       unsigned char *k, int *st, int open_state) {
    int j = body, closed = 0;
    while (j + 2 < len) {
        if (t[j] == '\\') { j += 2; continue; }
        if (t[j] == q && t[j + 1] == q && t[j + 2] == q) { j += 3; closed = 1; break; }
        j++;
    }
    if (!closed) j = len;
    mark(k, from, j, TK_STRING);
    *st = closed ? SYN_ST_NONE : open_state;
    return j;
}

/* Shared scanner for C, JavaScript/TypeScript and PHP. */
static int scan_c_like(SynLang lang, int st, const char *t, int len, unsigned char *k) {
    const char *kws = lang == SYN_C ? KW_C : lang == SYN_JS ? KW_JS : KW_PHP;
    int hash_comment = lang == SYN_PHP;
    int i = 0;

    if (st == SYN_ST_BLOCK_COMMENT) {
        i = mark_block(t, len, 0, 0, k, &st);
    } else if (st == SYN_ST_TEMPLATE) {
        i = mark_str(t, len, 0, 0, '`', 1, k, &st);
    } else if (lang == SYN_C) {
        int p = 0;
        while (p < len && (t[p] == ' ' || t[p] == '\t')) p++;
        if (p < len && t[p] == '#') {
            mark(k, 0, len, TK_PREPROC);
            return st;
        }
    }

    while (i < len) {
        char c = t[i];
        if ((c == '/' && i + 1 < len && t[i + 1] == '/') || (hash_comment && c == '#')) {
            mark(k, i, len, TK_COMMENT);
            break;
        }
        if (c == '/' && i + 1 < len && t[i + 1] == '*') {
            i = mark_block(t, len, i, i + 2, k, &st);
            continue;
        }
        if (c == '"' || c == '\'' || (c == '`' && lang == SYN_JS)) {
            i = mark_str(t, len, i, i + 1, c, c == '`', k, &st);
            continue;
        }
        if (isdigit(UC(c)) || (c == '.' && i + 1 < len && isdigit(UC(t[i + 1])))) {
            int j = i + 1;
            while (j < len && (isalnum(UC(t[j])) || t[j] == '.' || t[j] == '_')) j++;
            mark(k, i, j, TK_NUMBER);
            i = j;
            continue;
        }
        if (isalpha(UC(c)) || c == '_') {
            int j = i + 1;
            while (j < len && is_ident(t[j])) j++;
            if (is_word_in(kws, t + i, j - i)) mark(k, i, j, TK_KEYWORD);
            i = j;
            continue;
        }
        i++;
    }
    return st;
}

static int scan_py(int st, const char *t, int len, unsigned char *k) {
    int i = 0;

    if (st == SYN_ST_TRIPLE_DQ) {
        i = mark_triple(t, len, 0, 0, '"', k, &st, SYN_ST_TRIPLE_DQ);
    } else if (st == SYN_ST_TRIPLE_SQ) {
        i = mark_triple(t, len, 0, 0, '\'', k, &st, SYN_ST_TRIPLE_SQ);
    }

    while (i < len) {
        char c = t[i];
        if (c == '#') {
            mark(k, i, len, TK_COMMENT);
            break;
        }
        if (c == '"' || c == '\'') {
            if (i + 2 < len && t[i + 1] == c && t[i + 2] == c) {
                i = mark_triple(t, len, i, i + 3, c, k, &st,
                                c == '"' ? SYN_ST_TRIPLE_DQ : SYN_ST_TRIPLE_SQ);
                continue;
            }
            i = mark_str(t, len, i, i + 1, c, 0, k, &st);
            continue;
        }
        if (isdigit(UC(c))) {
            int j = i + 1;
            while (j < len && (isalnum(UC(t[j])) || t[j] == '.' || t[j] == '_')) j++;
            mark(k, i, j, TK_NUMBER);
            i = j;
            continue;
        }
        if (isalpha(UC(c)) || c == '_') {
            int j = i + 1;
            while (j < len && is_ident(t[j])) j++;
            if (is_word_in(KW_PY, t + i, j - i)) mark(k, i, j, TK_KEYWORD);
            i = j;
            continue;
        }
        i++;
    }
    return st;
}

/* JSON: a string followed by ':' is a key and gets the keyword color. */
static void scan_json(const char *t, int len, unsigned char *k) {
    int dummy = 0;
    int i = 0;
    while (i < len) {
        char c = t[i];
        if (c == '"') {
            int j = mark_str(t, len, i, i + 1, '"', 0, k, &dummy);
            int p = j;
            while (p < len && isspace(UC(t[p]))) p++;
            if (p < len && t[p] == ':') mark(k, i, j, TK_KEYWORD);
            i = j;
            continue;
        }
        if (isdigit(UC(c)) || (c == '-' && i + 1 < len && isdigit(UC(t[i + 1])))) {
            int j = i + 1;
            while (j < len && (isdigit(UC(t[j])) || t[j] == '.' || t[j] == 'e' || t[j] == 'E'
                               || t[j] == '+' || t[j] == '-')) j++;
            mark(k, i, j, TK_NUMBER);
            i = j;
            continue;
        }
        if (isalpha(UC(c))) {
            int j = i + 1;
            while (j < len && isalpha(UC(t[j]))) j++;
            if (is_word_in(KW_JSON, t + i, j - i)) mark(k, i, j, TK_KEYWORD);
            i = j;
            continue;
        }
        i++;
    }
}

/* Markdown: a line starting with '#' is a heading. */
static void scan_md(const char *t, int len, unsigned char *k) {
    if (len > 0 && t[0] == '#') mark(k, 0, len, TK_HEADING);
}

/* Shell: '#' comments (only where a word starts, so $# and ${#x} stay plain), "..." and '...'
   strings, $VAR and $1 left plain, keywords. Each line is independent. */
static void scan_sh(const char *t, int len, unsigned char *k) {
    int dummy = 0;
    int i = 0;
    while (i < len) {
        char c = t[i];
        if (c == '\\') {   /* escaped character outside quotes: plain */
            i += 2;
            continue;
        }
        if (c == '#' && (i == 0 || t[i - 1] == ' ' || t[i - 1] == '\t' || t[i - 1] == ';'
                         || t[i - 1] == '&' || t[i - 1] == '|' || t[i - 1] == '(')) {
            mark(k, i, len, TK_COMMENT);
            break;
        }
        if (c == '$') {
            int j = i + 1;
            if (j < len && t[j] == '\'') {   /* $'...' ANSI-C string: backslash escapes */
                int e = j + 1;
                while (e < len && t[e] != '\'') e += (t[e] == '\\') ? 2 : 1;
                if (e > len) e = len;
                if (e < len) e++;
                mark(k, i, e, TK_STRING);
                i = e;
                continue;
            }
            if (j < len && t[j] == '"') {    /* $"..." is a double-quoted string */
                i = mark_str(t, len, j, j + 1, '"', 0, k, &dummy);
                continue;
            }
            if (j < len && t[j] == '{') {
                while (j < len && t[j] != '}') j++;
                if (j < len) j++;
            } else if (j < len && (isalnum(UC(t[j])) || t[j] == '_')) {
                while (j < len && is_ident(t[j])) j++;
            } else if (j < len) {
                j++;   /* $#, $?, $$, $@ ... */
            }
            i = j;
            continue;
        }
        if (c == '"') {
            i = mark_str(t, len, i, i + 1, '"', 0, k, &dummy);
            continue;
        }
        if (c == '\'') {   /* no escapes inside single quotes */
            int j = i + 1;
            while (j < len && t[j] != '\'') j++;
            if (j < len) j++;
            mark(k, i, j, TK_STRING);
            i = j;
            continue;
        }
        if (isdigit(UC(c))) {
            int j = i + 1;
            while (j < len && (isalnum(UC(t[j])) || t[j] == '.' || t[j] == '_')) j++;
            mark(k, i, j, TK_NUMBER);
            i = j;
            continue;
        }
        if (isalpha(UC(c)) || c == '_') {
            int j = i + 1;
            while (j < len && is_ident(t[j])) j++;
            /* -if, --export are options, not keywords */
            if (!(i > 0 && t[i - 1] == '-') && is_word_in(KW_SH, t + i, j - i)) mark(k, i, j, TK_KEYWORD);
            i = j;
            continue;
        }
        i++;
    }
}

/* INI: ';' or '#' comments and [section] headings, after optional indent. */
static void scan_ini(const char *t, int len, unsigned char *k) {
    int p = 0;
    while (p < len && isspace(UC(t[p]))) p++;
    if (p >= len) return;
    if (t[p] == ';' || t[p] == '#') mark(k, p, len, TK_COMMENT);
    else if (t[p] == '[') mark(k, p, len, TK_HEADING);
}

/* Dot-files without a usable extension (~/.bashrc, ~/.zshrc, ...) that are shell scripts. */
static int sh_dotfile(const char *base) {
    static const char *names[] = {
        "bashrc", "bash_profile", "bash_aliases", "bash_logout", "profile",
        "zshrc", "zprofile", "zshenv", "zlogin", "zlogout", "kshrc", "mkshrc",
    };
    if (base[0] != '.') return 0;
    for (size_t n = 0; n < sizeof names / sizeof names[0]; n++)
        if (strcmp(base + 1, names[n]) == 0) return 1;
    return 0;
}

SynLang syntax_lang_for(const char *filename) {
    const char *dot = NULL, *base = filename;
    for (const char *p = filename; *p; p++) {
        if (*p == '/' || *p == '\\') { dot = NULL; base = p + 1; }
        else if (*p == '.') dot = p;
    }
    if (sh_dotfile(base)) return SYN_SH;
    if (!dot || !dot[1]) return SYN_PLAIN;

    static const struct { const char *ext; SynLang lang; } table[] = {
        {"c", SYN_C}, {"h", SYN_C}, {"cc", SYN_C}, {"cpp", SYN_C}, {"cxx", SYN_C},
        {"hh", SYN_C}, {"hpp", SYN_C}, {"hxx", SYN_C},
        {"js", SYN_JS}, {"mjs", SYN_JS}, {"cjs", SYN_JS}, {"jsx", SYN_JS},
        {"ts", SYN_JS}, {"tsx", SYN_JS},
        {"php", SYN_PHP},
        {"py", SYN_PY}, {"pyw", SYN_PY},
        {"json", SYN_JSON},
        {"md", SYN_MD}, {"markdown", SYN_MD},
        {"ini", SYN_INI},
        {"sh", SYN_SH}, {"bash", SYN_SH}, {"zsh", SYN_SH}, {"ksh", SYN_SH},
    };
    const char *ext = dot + 1;
    for (size_t n = 0; n < sizeof table / sizeof table[0]; n++) {
        const char *w = table[n].ext;
        const char *e = ext;
        int same = 1;
        for (; *w; w++, e++) {
            if (tolower(UC(*e)) != *w) { same = 0; break; }
        }
        if (same && *e == '\0') return table[n].lang;
    }
    return SYN_PLAIN;
}

int syntax_scan_line(SynLang lang, int st, const char *t, int len, unsigned char *k) {
    if (len > 0) mark(k, 0, len, TK_PLAIN);
    switch (lang) {
        case SYN_C:
        case SYN_JS:
        case SYN_PHP:
            return scan_c_like(lang, st, t, len, k);
        case SYN_PY:
            return scan_py(st, t, len, k);
        case SYN_JSON:
            scan_json(t, len, k);
            break;
        case SYN_MD:
            scan_md(t, len, k);
            break;
        case SYN_INI:
            scan_ini(t, len, k);
            break;
        case SYN_SH:
            scan_sh(t, len, k);
            break;
        default:
            break;
    }
    return SYN_ST_NONE;
}

/* Each entry sets its own background, so a token never inherits a wrong one.
   Order follows the TK_* enum. */
static const char *palette[4][7] = {
    /* Default: terminal colors */
    {"\x1b[0m", "\x1b[0;1;33m", "\x1b[0;32m", "\x1b[0;35m", "\x1b[0;36m", "\x1b[0;31m", "\x1b[0;1;33m"},
    /* Blue: white on blue */
    {"\x1b[0;37;44m", "\x1b[0;1;93;44m", "\x1b[0;96;44m", "\x1b[0;92;44m", "\x1b[0;95;44m",
     "\x1b[0;91;44m", "\x1b[0;1;93;44m"},
    /* Light: black on white */
    {"\x1b[0;30;47m", "\x1b[0;1;34;47m", "\x1b[0;32;47m", "\x1b[0;35;47m", "\x1b[0;31;47m",
     "\x1b[0;1;35;47m", "\x1b[0;1;34;47m"},
    /* Slate: VS Code-style colors on dark slate */
    {"\x1b[0;38;5;253;48;5;236m", "\x1b[0;1;38;5;75;48;5;236m", "\x1b[0;38;5;71;48;5;236m",
     "\x1b[0;38;5;216;48;5;236m", "\x1b[0;38;5;151;48;5;236m", "\x1b[0;38;5;176;48;5;236m",
     "\x1b[0;1;38;5;75;48;5;236m"},
};

const char *syntax_sgr(int kind, int scheme) {
    if (scheme < 0 || scheme > 3) scheme = 0;
    if (kind < 0 || kind > TK_HEADING) kind = TK_PLAIN;
    return palette[scheme][kind];
}

SynLang syntax_lang_effective(const char *filename, int override) {
    if (override > 0) return (SynLang)(override - 1);
    return syntax_lang_for(filename);
}

const char *syntax_lang_name(SynLang lang) {
    static const char *names[] = {"Plain", "C/C++", "JS/TS", "PHP", "Python", "JSON", "Markdown", "INI", "Shell"};
    if (lang < 0 || lang > SYN_SH) return names[0];
    return names[lang];
}
