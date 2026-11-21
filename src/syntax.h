/* Syntax highlighting: per-language line scanners that label each byte with a token kind. */
#ifndef SYNTAX_H
#define SYNTAX_H

typedef enum {
    SYN_PLAIN = 0,
    SYN_C,        /* C and C++ */
    SYN_JS,       /* JavaScript, TypeScript */
    SYN_PHP,
    SYN_PY,
    SYN_JSON,
    SYN_MD,
    SYN_INI,
    SYN_SH        /* shell: sh, bash, zsh, ksh */
} SynLang;

/* Token kinds, one per byte. */
enum {
    TK_PLAIN = 0,
    TK_KEYWORD,
    TK_COMMENT,
    TK_STRING,
    TK_NUMBER,
    TK_PREPROC,   /* C preprocessor line */
    TK_HEADING    /* Markdown heading, INI section */
};

/* Parser state carried from one line to the next. */
enum {
    SYN_ST_NONE = 0,
    SYN_ST_BLOCK_COMMENT,   /* inside a C-style block comment */
    SYN_ST_TEMPLATE,        /* inside a JS `...` string */
    SYN_ST_TRIPLE_DQ,       /* inside a Python """ string */
    SYN_ST_TRIPLE_SQ        /* inside a Python ''' string */
};

/* Language for a file name, by extension. SYN_PLAIN when unknown. */
SynLang syntax_lang_for(const char *filename);

/* Language to use for a document: override 0 means auto (by extension), otherwise override is SynLang + 1. */
SynLang syntax_lang_effective(const char *filename, int override);

/* Display name of a language, e.g. "C/C++". */
const char *syntax_lang_name(SynLang lang);

/* How many lines above the top of the screen are scanned to get the parser state there. */
#define SYN_LOOKBACK 1000

/* Scans one line. state_in is the state at the start of the line; the
   returned value is the state at its end. kinds[0..len-1] receives a TK_* per byte. */
int syntax_scan_line(SynLang lang, int state_in, const char *text, int len, unsigned char *kinds);

/* SGR sequence that colors a token kind for a color scheme (0 Default, 1 Blue, 2 Light, 3 Slate). */
const char *syntax_sgr(int kind, int scheme);

#endif
