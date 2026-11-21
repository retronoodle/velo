/* Unit tests for syntax.c. Build and run with `make test`. */
#include <stdio.h>
#include <string.h>

#include "syntax.h"

static int failures = 0;
static int checks = 0;

#define CHECK(cond, what) do { \
    checks++; \
    if (!(cond)) { failures++; printf("FAIL: %s (line %d)\n", what, __LINE__); } \
} while (0)

/* True if kinds[from..to) all equal kind. */
static int all_kind(const unsigned char *k, int from, int to, int kind) {
    for (int i = from; i < to; i++) if (k[i] != kind) return 0;
    return 1;
}

/* Scans one line and returns the state after it. */
static int scan(SynLang lang, int st, const char *s, unsigned char *k) {
    return syntax_scan_line(lang, st, s, (int)strlen(s), k);
}

static void test_lang_for(void) {
    CHECK(syntax_lang_for("main.c") == SYN_C, "c extension");
    CHECK(syntax_lang_for("x/y.h") == SYN_C, "header extension");
    CHECK(syntax_lang_for("C:\\src\\page.PHP") == SYN_PHP, "php, upper case, windows path");
    CHECK(syntax_lang_for("app.ts") == SYN_JS, "ts maps to js scanner");
    CHECK(syntax_lang_for("tool.py") == SYN_PY, "python extension");
    CHECK(syntax_lang_for("data.json") == SYN_JSON, "json extension");
    CHECK(syntax_lang_for("README.md") == SYN_MD, "markdown extension");
    CHECK(syntax_lang_for("untitled.txt") == SYN_PLAIN, "txt is plain");
    CHECK(syntax_lang_for("noext") == SYN_PLAIN, "no extension is plain");
    CHECK(syntax_lang_for("dir.v2/file") == SYN_PLAIN, "dot in directory is not an extension");
    CHECK(syntax_lang_for("") == SYN_PLAIN, "empty name is plain");
}

static void test_c(void) {
    unsigned char k[256];
    int st;

    st = scan(SYN_C, SYN_ST_NONE, "int x;", k);
    CHECK(all_kind(k, 0, 3, TK_KEYWORD), "int is a keyword");
    CHECK(k[4] == TK_PLAIN, "identifier x is plain");
    CHECK(st == SYN_ST_NONE, "no state after a plain line");

    scan(SYN_C, SYN_ST_NONE, "x // hi", k);
    CHECK(k[0] == TK_PLAIN && all_kind(k, 2, 7, TK_COMMENT), "line comment to end of line");

    scan(SYN_C, SYN_ST_NONE, "#include <stdio.h>", k);
    CHECK(all_kind(k, 0, 18, TK_PREPROC), "preprocessor line");

    st = scan(SYN_C, SYN_ST_NONE, "a /* start", k);
    CHECK(st == SYN_ST_BLOCK_COMMENT, "block comment left open");
    CHECK(all_kind(k, 2, 10, TK_COMMENT), "block comment colored to end of line");

    st = scan(SYN_C, st, "still */ int", k);
    CHECK(st == SYN_ST_NONE, "block comment closed on next line");
    CHECK(all_kind(k, 0, 8, TK_COMMENT), "comment continues then closes");
    CHECK(all_kind(k, 9, 12, TK_KEYWORD), "keyword after comment is colored");

    st = scan(SYN_C, SYN_ST_NONE, "s = \"a\\\"b\" + 1", k);
    CHECK(st == SYN_ST_NONE, "escaped quote does not open a string");
    CHECK(all_kind(k, 4, 10, TK_STRING), "string with escaped quote");
    CHECK(k[13] == TK_NUMBER, "number after string");
}

static void test_js(void) {
    unsigned char k[256];
    int st;

    st = scan(SYN_JS, SYN_ST_NONE, "const s = `abc", k);
    CHECK(st == SYN_ST_TEMPLATE, "template string open at end of line");
    CHECK(all_kind(k, 10, 14, TK_STRING), "template body colored");

    st = scan(SYN_JS, st, "def` return 0.5", k);
    CHECK(st == SYN_ST_NONE, "template closed on next line");
    CHECK(all_kind(k, 0, 4, TK_STRING), "template close colored");
    CHECK(all_kind(k, 5, 11, TK_KEYWORD), "return after template is keyword");
    CHECK(all_kind(k, 12, 15, TK_NUMBER), "decimal number");
}

static void test_py(void) {
    unsigned char k[256];
    int st;

    st = scan(SYN_PY, SYN_ST_NONE, "x = \"\"\"doc", k);
    CHECK(st == SYN_ST_TRIPLE_DQ, "triple-quoted string open");
    CHECK(all_kind(k, 4, 10, TK_STRING), "triple-quoted body colored");

    st = scan(SYN_PY, st, "end\"\"\" def f(): # note", k);
    CHECK(st == SYN_ST_NONE, "triple-quoted string closed");
    CHECK(all_kind(k, 0, 6, TK_STRING), "triple-quote close colored");
    CHECK(all_kind(k, 7, 10, TK_KEYWORD), "def is a keyword");
    CHECK(all_kind(k, 16, 22, TK_COMMENT), "hash comment");
}

static void test_json(void) {
    unsigned char k[256];
    scan(SYN_JSON, SYN_ST_NONE, "  \"name\": \"x\", 3, true", k);
    CHECK(all_kind(k, 2, 8, TK_KEYWORD), "key before colon is keyword");
    CHECK(all_kind(k, 10, 13, TK_STRING), "string value");
    CHECK(k[15] == TK_NUMBER, "number value");
    CHECK(all_kind(k, 18, 22, TK_KEYWORD), "true literal");
}

static void test_md_ini(void) {
    unsigned char k[256];
    scan(SYN_MD, SYN_ST_NONE, "## Title", k);
    CHECK(all_kind(k, 0, 8, TK_HEADING), "markdown heading");
    scan(SYN_MD, SYN_ST_NONE, "plain # not", k);
    CHECK(all_kind(k, 0, 11, TK_PLAIN), "hash mid-line is not a heading");

    scan(SYN_INI, SYN_ST_NONE, "[core]", k);
    CHECK(all_kind(k, 0, 6, TK_HEADING), "ini section");
    scan(SYN_INI, SYN_ST_NONE, "  ; note", k);
    CHECK(k[0] == TK_PLAIN && all_kind(k, 2, 8, TK_COMMENT), "ini comment after indent");
}

static void test_sh(void) {
    unsigned char k[256];
    scan(SYN_SH, SYN_ST_NONE, "if [ -f \"$1\" ]; then # check", k);
    /* "if [ -f \"$1\" ]; then # check": if at 0, "$1" string at 8..11, then at 16..19, # at 21 */
    CHECK(all_kind(k, 0, 2, TK_KEYWORD), "if is a keyword");
    CHECK(k[3] == TK_PLAIN, "bracket is plain");
    CHECK(all_kind(k, 8, 12, TK_STRING), "double-quoted string");
    CHECK(all_kind(k, 16, 20, TK_KEYWORD), "then is a keyword");
    CHECK(all_kind(k, 21, 28, TK_COMMENT), "hash after space is a comment");

    /* "echo $# ${#x} $HOME $if" */
    scan(SYN_SH, SYN_ST_NONE, "echo $# ${#x} $HOME $if", k);
    CHECK(all_kind(k, 5, 7, TK_PLAIN), "$# is not a comment");
    CHECK(all_kind(k, 8, 13, TK_PLAIN), "${#x} is not a comment");
    CHECK(all_kind(k, 20, 23, TK_PLAIN), "$if is not a keyword");

    /* "echo 'it s' 42 --export" */
    scan(SYN_SH, SYN_ST_NONE, "echo 'it s' 42 --export", k);
    CHECK(all_kind(k, 5, 11, TK_STRING), "single-quoted string");
    CHECK(k[12] == TK_NUMBER, "number");
    CHECK(all_kind(k, 15, 23, TK_PLAIN), "--export option is not a keyword");
    CHECK(syntax_lang_for("build.sh") == SYN_SH && syntax_lang_for("rc.BASH") == SYN_SH, "shell extensions");
    CHECK(syntax_lang_for("/home/me/.bashrc") == SYN_SH && syntax_lang_for("~/.zshrc") == SYN_SH,
          "shell dot-files");
    CHECK(syntax_lang_for("/home/me/.bashrc.txt") == SYN_PLAIN && syntax_lang_for(".vimrc") == SYN_PLAIN,
          "other dot-files stay plain");
    scan(SYN_SH, SYN_ST_NONE, "echo it\\'s fine # ok", k);
    CHECK(all_kind(k, 0, 16, TK_PLAIN) && all_kind(k, 16, 20, TK_COMMENT), "escaped quote is not a string");
    scan(SYN_SH, SYN_ST_NONE, "printf $'%s\\n' a; echo 'b' # c", k);
    CHECK(all_kind(k, 7, 14, TK_STRING) && all_kind(k, 23, 26, TK_STRING), "$'...' is one string");
    CHECK(all_kind(k, 27, 30, TK_COMMENT), "comment after $'...' is still a comment");
    CHECK(strcmp(syntax_lang_name(SYN_SH), "Shell") == 0, "shell name");
}

static void test_sgr(void) {
    CHECK(syntax_sgr(TK_KEYWORD, 0) != syntax_sgr(TK_PLAIN, 0), "keyword has its own color");
    CHECK(strcmp(syntax_sgr(TK_STRING, 1), syntax_sgr(TK_STRING, 2)) != 0, "schemes differ");
    CHECK(syntax_sgr(99, 0) != NULL && syntax_sgr(TK_KEYWORD, 7) != NULL, "bad inputs fall back");
}

static void test_override(void) {
    CHECK(syntax_lang_effective("notes.txt", 0) == SYN_PLAIN, "auto: txt is plain");
    CHECK(syntax_lang_effective("main.c", 0) == SYN_C, "auto: follows extension");
    CHECK(syntax_lang_effective("main.c", SYN_PY + 1) == SYN_PY, "override wins over extension");
    CHECK(syntax_lang_effective("main.c", SYN_PLAIN + 1) == SYN_PLAIN, "override can force plain");
    CHECK(strcmp(syntax_lang_name(SYN_C), "C/C++") == 0, "C name");
    CHECK(strcmp(syntax_lang_name(SYN_PLAIN), "Plain") == 0, "plain name");
}

int main(void) {
    test_lang_for();
    test_override();
    test_c();
    test_js();
    test_py();
    test_json();
    test_md_ini();
    test_sh();
    test_sgr();

    printf("%d checks, %d failures\n", checks, failures);
    return failures ? 1 : 0;
}
