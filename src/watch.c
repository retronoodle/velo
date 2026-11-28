/* Detects when the file open in the active tab changes on disk. Polled from the
   main loop's idle tick (stat only, no threads), so it also works on Windows.
   Only named tabs are checked. The recorded state lives in Editor, so it moves
   with the tab. */
#define _POSIX_C_SOURCE 200809L   /* st_mtim (nanosecond mtime) under -std=c99 */
#ifdef __APPLE__
    #define _DARWIN_C_SOURCE      /* st_mtimespec is hidden by _POSIX_C_SOURCE on macOS */
#endif
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>

#include "editor.h"
#include "buffer.h"
#include "term.h"
#include "ui.h"
#include "clipboard.h"
#include "files.h"
#include "undo.h"
#include "recovery.h"
#include "tabs.h"
#include "options.h"
#include "watch.h"

/* Reads the file's mtime (seconds and nanoseconds) and size. Returns 0 if it cannot be stat'ed.
   Windows' stat has only whole seconds, so its nanoseconds are always 0. */
static int disk_state(const char *name, long *mtime, long *mtime_ns, long *size) {
    struct stat st;
    if (stat(name, &st) != 0) return 0;
    *mtime = (long)st.st_mtime;
#if defined(__APPLE__)
    *mtime_ns = (long)st.st_mtimespec.tv_nsec;
#elif !defined(_WIN32)
    *mtime_ns = (long)st.st_mtim.tv_nsec;
#else
    *mtime_ns = 0;
#endif
    *size = (long)st.st_size;
    return 1;
}

void watch_record(void) {
    long mtime = 0, mtime_ns = 0, size = 0;
    if (!disk_state(editor.filename, &mtime, &mtime_ns, &size)) {
        mtime = 0;
        mtime_ns = 0;
        size = 0;
    }
    editor.disk_mtime = mtime;
    editor.disk_mtime_ns = mtime_ns;
    editor.disk_size = size;
}

/* Reloads the file from disk, keeping the cursor where it was (clamped). */
static void reload_from_disk(void) {
    FILE *in = fopen(editor.filename, "rb");
    if (!in) {
        show_message("Could not reload the file.");
        watch_record();
        return;
    }

    int cy = editor.cursor_y;
    int cx = editor.cursor_x;
    int sy = editor.scroll_y;

    load_stream(in);   /* clears undo and sets the line ending from the file */
    fclose(in);

    if (cy >= editor.line_count) cy = editor.line_count - 1;
    if (cy < 0) cy = 0;
    if (cx > editor.lines[cy].len) cx = editor.lines[cy].len;
    /* don't stop inside a UTF-8 sequence */
    while (cx > 0 && (((unsigned char)editor.lines[cy].text[cx]) & 0xC0) == 0x80) cx--;

    editor.cursor_y = cy;
    editor.cursor_x = cx;
    editor.scroll_y = sy;   /* adjust_scroll() corrects it on the next pass */
    sel_clear();
    editor.modified = 0;
    recovery_discard_name(editor.filename);   /* any backup predates the disk version */
    watch_record();
    show_message("Reloaded from disk.");
}

/* Asks whether to reload. Yes reloads; any other answer is remembered for this
   disk state so the question is not repeated. */
static void ask_reload(void) {
    const char *msg = editor.modified
        ? "File changed on disk. Reload and lose your changes? (Y)es / (N)o"
        : "File changed on disk. Reload? (Y)es / (N)o";

    gotoxy(0, screen_h - 2);
    printf("\x1b[7m%-*.*s%s", screen_w - 1, screen_w - 1, msg, options_base_sgr());
    fflush(stdout);

    while (1) {
        int c = getch_prompt();
        if (c == 'y' || c == 'Y') {
            reload_from_disk();
            return;
        }
        if (c == -1) continue;   /* POSIX read timeout (VTIME), not an answer: keep waiting */
        if (c == 'n' || c == 'N' || c == 27) {
            watch_record();   /* keep the buffer; stop asking about this change */
            return;
        }
    }
}

int watch_tick(void) {
    if (!tab_named(tab_current())) return 0;
    if (editor.filename[0] == '\0') return 0;

    long mtime = 0, mtime_ns = 0, size = 0;
    if (!disk_state(editor.filename, &mtime, &mtime_ns, &size)) return 0;   /* deleted or moved: stay quiet */
    if (mtime == editor.disk_mtime && mtime_ns == editor.disk_mtime_ns && size == editor.disk_size) return 0;

    ask_reload();
    return 1;
}
