/* Search: Find/Find Next (case-insensitive, wraps), Replace (Y/N/A, from cursor to end), Go to Line. */
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "editor.h"
#include "buffer.h"
#include "term.h"
#include "ui.h"
#include "undo.h"
#include "clipboard.h"
#include "search.h"

static char last_find[256] = "";   /* remembered for Find Next (F3) */

static int match_at(const Line *ln, int x, const char *pat, int plen) {
    if (x + plen > ln->len) return 0;
    for (int i = 0; i < plen; i++) {
        if (tolower((unsigned char)ln->text[x + i]) != tolower((unsigned char)pat[i])) return 0;
    }
    return 1;
}

/* Finds the first match at or after (sy, sx). If wrap is set, the search
   continues from the top of the document; otherwise it stops at the end.
   Returns 1 and sets (*fy, *fx) on a hit. */
static int find_from(int sy, int sx, int wrap, int *fy, int *fx) {
    int plen = (int)strlen(last_find);
    int lines = editor.line_count;
    if (plen == 0 || lines == 0) return 0;

    int visits = wrap ? lines + 1 : lines - sy;   /* wrap revisits the start line once */
    for (int k = 0; k < visits; k++) {
        int y = (sy + k) % lines;
        int x0 = (k == 0) ? sx : 0;
        const Line *ln = &editor.lines[y];
        for (int x = x0; x + plen <= ln->len; x++) {
            if (match_at(ln, x, last_find, plen)) {
                *fy = y;
                *fx = x;
                return 1;
            }
        }
    }
    return 0;
}

/* Selects a match: anchor at its start, cursor at its end (drawn in reverse video). */
static void select_match(int y, int x) {
    editor.sel_active = 1;
    editor.sel_block = 0;
    editor.sel_y = y;
    editor.sel_x = x;
    editor.cursor_y = y;
    editor.cursor_x = x + (int)strlen(last_find);
    undo_break_group();
    adjust_scroll();
}

/* Redraws with the current match shown, then asks Y/N/A. Returns 'y', 'n', 'a' or 27 (Esc). */
static int ask_replace(void) {
    refresh_screen();
    gotoxy(0, screen_h - 2);
    printf("%-*s", screen_w - 1, "Replace? (Y)es  (N)o  (A)ll  (Esc)");
    fflush(stdout);

    for (;;) {
        int c = getch_prompt();
        if (c == -1 || c == 27) return 27;
        if (c == 'y' || c == 'Y') return 'y';
        if (c == 'n' || c == 'N') return 'n';
        if (c == 'a' || c == 'A') return 'a';
    }
}

void search_find(void) {
    char buf[256];
    sel_clear();
    if (!prompt_line("Find: ", buf, sizeof buf)) return;
    strcpy(last_find, buf);
    search_next();
}

void search_next(void) {
    if (last_find[0] == '\0') {
        search_find();
        return;
    }

    sel_clear();
    int fy, fx;
    if (find_from(editor.cursor_y, editor.cursor_x, 1, &fy, &fx)) {
        select_match(fy, fx);
    } else {
        show_message("Not found.");
    }
}

void search_replace(void) {
    char find[256], repl[256];
    sel_clear();
    if (!prompt_line("Find: ", find, sizeof find)) return;
    if (!prompt_line_opt("Replace with: ", repl, sizeof repl)) return;
    strcpy(last_find, find);

    int plen = (int)strlen(find);
    int rlen = (int)strlen(repl);
    int count = 0, all = 0;
    int y = editor.cursor_y, x = editor.cursor_x;
    int fy, fx;

    /* The whole run is one undo step. */
    undo_begin_group();
    while (find_from(y, x, 0, &fy, &fx)) {
        select_match(fy, fx);

        int replace_it = all;
        if (!all) {
            int key = ask_replace();
            if (key == 27) break;
            if (key == 'a') all = 1;
            replace_it = (key == 'y' || key == 'a');
        }

        if (replace_it) {
            sel_clear();
            editor.cursor_y = fy;
            editor.cursor_x = fx;
            delete_range(fy, fx, fy, fx + plen);
            insert_text(repl, rlen);   /* inserts at the cursor, which is now fx */
            count++;
            y = editor.cursor_y;
            x = editor.cursor_x;       /* continue after the inserted text */
        } else {
            y = fy;
            x = fx + plen;
        }
    }
    undo_end_group();
    sel_clear();

    if (count == 0) {
        show_message("Not found.");
    } else {
        char msg[64];
        snprintf(msg, sizeof msg, "Replaced %d.", count);
        show_message(msg);
    }
}

void search_goto_line(void) {
    char buf[32];
    sel_clear();
    if (!prompt_line("Go to line: ", buf, sizeof buf)) return;

    char *end;
    long n = strtol(buf, &end, 10);
    if (*end != '\0') {
        show_message("Not a line number.");
        return;
    }
    if (n < 1) n = 1;
    if (n > editor.line_count) n = editor.line_count;

    editor.cursor_y = (int)n - 1;
    editor.cursor_x = 0;
    move_cursor(0, 0);   /* clamps the cursor and scrolls it into view */
}
