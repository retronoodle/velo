/* File operations: save, save-as, new, open (with a folder browser), recent files, exit. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

#ifdef _WIN32
    #include <direct.h>
    #include <windows.h>
#else
    #include <dirent.h>
    #include <sys/stat.h>
    #include <unistd.h>
#endif

#include "editor.h"
#include "buffer.h"
#include "term.h"
#include "ui.h"
#include "options.h"
#include "files.h"
#include "undo.h"
#include "recovery.h"
#include "watch.h"
#include "tabs.h"
#include "recent.h"
#include "utf8.h"

/* ---- File operations. Each returns 1 on success, 0 if cancelled/failed. ---- */

static int write_file(const char *name) {
    /* Binary mode: Windows must not add its own CR, the line ending comes from options */
    FILE *file = fopen(name, "wb");
    if (!file) {
        show_message("Error: could not save file.");
        return 0;
    }
    const char *eol = options.crlf ? "\r\n" : "\n";
    for (int i = 0; i < editor.line_count; i++) {
        fwrite(editor.lines[i].text, 1, editor.lines[i].len, file);
        fputs(eol, file);
    }
    fclose(file);
    editor.modified = 0;
    undo_mark_saved();
    recovery_discard_name(name);   /* the backup is stale now that the file is saved */
    watch_record();                /* our own write is not an outside change */

    char msg[320];   /* room for "Saved " plus a full file name */
    snprintf(msg, sizeof(msg), "Saved %s", name);
    show_message(msg);
    return 1;
}

int do_save_as(void);
static int browse(int save_mode, const char *suggest, char *out, size_t size);

int do_save(void) {
    if (!tab_named(tab_current())) {
        return do_save_as();
    }
    return write_file(editor.filename);
}

/* Asks before replacing a file that exists. Returns 1 for Yes. */
static int confirm_overwrite(const char *name) {
    char msg[320];
    snprintf(msg, sizeof(msg), "%s exists. Overwrite? (Y)es / (N)o", name);
    gotoxy(0, screen_h - 2);
    printf("%-*.*s", screen_w - 1, screen_w - 1, msg);
    fflush(stdout);

    while (1) {
        int c = getch_prompt();
        if (c == 'y' || c == 'Y') return 1;
        if (c == 'n' || c == 'N' || c == 27) return 0;
    }
}

static int file_exists(const char *name) {
    FILE *f = fopen(name, "rb");
    if (!f) return 0;
    fclose(f);
    return 1;
}

static const char *base_name(const char *path) {
    const char *s = path;
    for (const char *p = path; *p; p++) {
        if (*p == '/' || *p == '\\') s = p + 1;
    }
    return s;
}

int do_save_as(void) {
    char name[256], old[256], suggest[256];
    int named = tab_named(tab_current());
    if (named) snprintf(suggest, sizeof(suggest), "%s", base_name(editor.filename));
    else snprintf(suggest, sizeof(suggest), "%s", "untitled.txt");

    if (browse(1, suggest, name, sizeof(name)) != 1) return 0;

    int same_as_current = named && strcmp(name, editor.filename) == 0;
    if (!same_as_current && file_exists(name) && !confirm_overwrite(name)) return 0;

    snprintf(old, sizeof(old), "%s", editor.filename);
    if (!write_file(name)) return 0;
    strcpy(editor.filename, name);
    watch_record();   /* write_file recorded the old name; record the new file's state */
    tab_mark_named();
    recent_add(name);
    if (strcmp(old, name) != 0) recovery_discard_name(old);   /* old name's backup is stale */
    return 1;
}

/* Asks to save unsaved changes in the active tab. Returns 1 to go ahead (saved
   or discarded), 0 if the user cancelled with Esc or the save failed. */
static int confirm_discard(void) {
    if (!editor.modified) return 1;

    gotoxy(0, screen_h - 2);
    printf("\x1b[7m%-*s%s", screen_w - 1, "Save changes? (Y)es / (N)o / Esc = cancel", options_base_sgr());
    fflush(stdout);

    while (1) {
        int c = getch_prompt();
        if (c == 'y' || c == 'Y') return do_save();
        if (c == 'n' || c == 'N') {
            recovery_discard_name(editor.filename);   /* changes are being thrown away */
            editor.modified = 0;
            return 1;
        }
        if (c == 27) return 0;
    }
}

int do_new(void) {
    if (!tab_new()) {
        show_message("Too many tabs (16 at most).");
        return 0;
    }
    return 1;
}

/* Closes the active tab, asking first if it has unsaved changes. */
int do_close_tab(void) {
    if (!confirm_discard()) return 0;
    recovery_discard_name(editor.filename);
    tab_close_current();
    return 1;
}

/* ---- File browser (Open and Save As) ---- */

#define MAX_DIR_ENTRIES 512
/* List rows sit between the "Open:" line (row 1) and the two footer rows. */
static int browser_rows(void) {
    return screen_h - 5;
}

typedef struct {
    char name[256];
    int is_dir;
} DirEntry;

static DirEntry dir_entries[MAX_DIR_ENTRIES];
static int dir_count = 0;

static void add_dir_entry(const char *name, int is_dir) {
    snprintf(dir_entries[dir_count].name, sizeof(dir_entries[0].name), "%s", name);
    dir_entries[dir_count].is_dir = is_dir;
    dir_count++;
}

/* Folders first, then names, case-insensitive. Index 0 ("..") is excluded. */
static int dir_entry_cmp(const void *a, const void *b) {
    const DirEntry *x = a, *y = b;
    if (x->is_dir != y->is_dir) return y->is_dir - x->is_dir;
    const unsigned char *p = (const unsigned char *)x->name;
    const unsigned char *q = (const unsigned char *)y->name;
    while (*p && tolower(*p) == tolower(*q)) { p++; q++; }
    return tolower(*p) - tolower(*q);
}

static void load_dir_entries(void) {
    dir_count = 0;
    add_dir_entry("..", 1);

#ifdef _WIN32
    WIN32_FIND_DATAA fd;
    HANDLE h = FindFirstFileA("*", &fd);
    if (h != INVALID_HANDLE_VALUE) {
        do {
            if (strcmp(fd.cFileName, ".") == 0 || strcmp(fd.cFileName, "..") == 0) continue;
            if (dir_count >= MAX_DIR_ENTRIES) break;
            add_dir_entry(fd.cFileName, (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0);
        } while (FindNextFileA(h, &fd));
        FindClose(h);
    }
#else
    DIR *d = opendir(".");
    if (d) {
        struct dirent *de;
        while ((de = readdir(d)) != NULL) {
            if (strcmp(de->d_name, ".") == 0 || strcmp(de->d_name, "..") == 0) continue;
            if (dir_count >= MAX_DIR_ENTRIES) break;
            struct stat st;
            int is_dir = (stat(de->d_name, &st) == 0 && S_ISDIR(st.st_mode));
            add_dir_entry(de->d_name, is_dir);
        }
        closedir(d);
    }
#endif

    qsort(dir_entries + 1, dir_count - 1, sizeof(DirEntry), dir_entry_cmp);
}

static void current_dir(char *buf, size_t size) {
#ifdef _WIN32
    if (!_getcwd(buf, (int)size)) buf[0] = '\0';
#else
    if (!getcwd(buf, size)) buf[0] = '\0';
#endif
}

static int change_dir(const char *path) {
#ifdef _WIN32
    return _chdir(path) == 0;
#else
    return chdir(path) == 0;
#endif
}

/* Save-as name field: the name the file will get. */
static int is_text_key(int key) {
    return key >= 32 && key != 127 && (key < 0x80 || key >= 0xA0) && key < KEY_BASE;
}

static void name_append(char *typed, size_t size, int key) {
    char enc[4];
    int n = utf8_encode(key, enc);
    size_t len = strlen(typed);
    if (len + (size_t)n + 1 > size) return;   /* name is full */
    memcpy(typed + len, enc, n);
    typed[len + n] = '\0';
}

static void name_backspace(char *typed) {
    size_t len = strlen(typed);
    if (len == 0) return;
    len--;
    while (len > 0 && ((unsigned char)typed[len] & 0xC0) == 0x80) len--;   /* whole UTF-8 character */
    typed[len] = '\0';
}

/* In Save As, moving onto a file copies its name into the name field. */
static void follow_selection(int save_mode, int sel, char *typed, size_t size) {
    if (save_mode && !dir_entries[sel].is_dir) {
        snprintf(typed, size, "%s", dir_entries[sel].name);
    }
}

static void draw_browser(int sel, int top, int save_mode, const char *typed) {
    char cwd[512];
    char line[512];   /* large enough for any path; fb_printf clips to the screen width */
    current_dir(cwd, sizeof(cwd));

    fb_reset();
    fb_puts("\x1b[?25l");

    snprintf(line, sizeof(line), "%s", save_mode ? " Save as: " : " Open: ");
    strncat(line, cwd, sizeof(line) - strlen(line) - 1);
    fb_gotoxy(0, 1);
    fb_printf("\x1b[7m%-*.*s%s", screen_w - 1, screen_w - 1, line, options_base_sgr());

    for (int row = 0; row < browser_rows(); row++) {
        int idx = top + row;
        fb_gotoxy(0, 2 + row);
        if (idx < dir_count) {
            int hl = (idx == sel);
            if (hl) fb_puts("\x1b[7m");
            snprintf(line, sizeof(line), "  %s%s",
                     dir_entries[idx].is_dir ? "[DIR] " : "      ",
                     dir_entries[idx].name);
            fb_printf("%-*.*s", screen_w - 1, screen_w - 1, line);
            if (hl) fb_puts(options_base_sgr());
        } else {
            fb_printf("%-*s", screen_w - 1, "");
        }
    }

    fb_gotoxy(0, screen_h - 3);
    fb_printf("\x1b[7m%-*s%s", screen_w - 1,
              save_mode ? " Enter=save  Esc=cancel  Up/Down=move"
                        : " Enter=open  Esc=cancel  T=type name  R=recent  Up/Down=move",
              options_base_sgr());
    fb_gotoxy(0, screen_h - 2);
    if (save_mode) {
        snprintf(line, sizeof(line), " Name: %s_", typed);
        fb_printf("%-*.*s", screen_w - 1, screen_w - 1, line);
    } else {
        fb_printf("%-*s", screen_w - 1, "");
    }

    fb_flush();
}

/* Like read_key(), but lets mouse events through (the browser handles them). */
static int browser_read_key(void) {
    while (1) {
        int k = read_key_wait(-1);
        if (k == KEY_PASTE) {
            int len;
            free(term_take_paste(&len));
            continue;
        }
        if (k == -1) continue;   /* idle timeout (POSIX) */
        return k;
    }
}

/* Shows a file browser over the editor area. Entering a folder changes the
   working directory to it, so the chosen file name is relative to it.
   Open mode returns 1 with the file name in out. Save mode returns 1 with the
   typed name in out. Open mode returns 2 when the user asks for the recent
   list. Returns 0 if cancelled. */
static int browse(int save_mode, const char *suggest, char *out, size_t size) {
    int sel = 0, top = 0;
    int typing = 0;   /* Save As: user is typing a name, so Enter saves instead of entering a folder */
    char typed[256];
    snprintf(typed, sizeof(typed), "%s", suggest ? suggest : "");
    load_dir_entries();
    follow_selection(save_mode, sel, typed, sizeof(typed));

    while (1) {
        if (sel < top) top = sel;
        if (sel >= top + browser_rows()) top = sel - browser_rows() + 1;
        draw_browser(sel, top, save_mode, typed);

        int key = browser_read_key();
        if (key == KEY_MOUSE) {
            MouseEvent ev;
            term_get_mouse(&ev);
            if (ev.kind == MEV_WHEEL_UP || ev.kind == MEV_WHEEL_DOWN) {
                int max_top = dir_count - browser_rows();
                if (max_top < 0) max_top = 0;
                top += (ev.kind == MEV_WHEEL_UP) ? -3 : 3;
                if (top < 0) top = 0;
                if (top > max_top) top = max_top;
                if (sel < top) sel = top;
                if (sel >= top + browser_rows()) sel = top + browser_rows() - 1;
                if (sel >= dir_count) sel = dir_count - 1;
                follow_selection(save_mode, sel, typed, sizeof(typed));
                typing = 0;
                continue;
            }
            if (ev.kind != MEV_PRESS || ev.y < 2 || ev.y >= 2 + browser_rows()) continue;
            int idx = top + (ev.y - 2);
            if (idx >= dir_count) continue;
            typing = 0;
            sel = idx;
            follow_selection(save_mode, sel, typed, sizeof(typed));
            /* Save As on a file only selects it (the name is filled in); Enter saves */
            if (save_mode && !dir_entries[sel].is_dir) continue;
            key = KEY_ENTER;   /* a click opens a folder or picks a file, like Enter */
        }
        switch (key) {
            case KEY_UP:
                if (sel > 0) sel--;
                typing = 0;
                follow_selection(save_mode, sel, typed, sizeof(typed));
                break;
            case KEY_DOWN:
                if (sel < dir_count - 1) sel++;
                typing = 0;
                follow_selection(save_mode, sel, typed, sizeof(typed));
                break;
            case KEY_RESIZE:
                term_update_size();
                clear_screen();
                break;
            case KEY_ESC:
                return 0;
            case KEY_BACKSPACE:
                if (save_mode) { name_backspace(typed); typing = 1; }
                break;
            case KEY_ENTER:
                if (save_mode && typing && typed[0]) {
                    snprintf(out, size, "%s", typed);
                    return 1;
                }
                if (dir_entries[sel].is_dir) {
                    if (change_dir(dir_entries[sel].name)) {
                        load_dir_entries();
                        sel = 0;
                        top = 0;
                        typing = 0;
                        follow_selection(save_mode, sel, typed, sizeof(typed));
                    } else {
                        show_message("Cannot open that folder.");
                    }
                } else if (!save_mode) {
                    snprintf(out, size, "%s", dir_entries[sel].name);
                    return 1;
                } else {
                    /* Save: the typed name, which may be a path (used as given) */
                    if (typed[0] == '\0') {
                        show_message("Type a file name first.");
                        break;
                    }
                    snprintf(out, size, "%s", typed);
                    return 1;
                }
                break;
            default:
                if (!save_mode) {
                    if (key == 't' || key == 'T') {
                        if (prompt_line("Open: ", out, (int)size)) return 1;
                    } else if (key == 'r' || key == 'R') {
                        if (recent_count() > 0) return 2;
                        show_message("No recent files.");
                    }
                } else if (is_text_key(key)) {
                    name_append(typed, sizeof(typed), key);
                    typing = 1;
                }
                break;
        }
    }
}

/* ---- Recent files list ---- */

static void draw_recent(int sel, int top) {
    char line[512];
    fb_reset();
    fb_puts("\x1b[?25l");

    fb_gotoxy(0, 1);
    fb_printf("\x1b[7m%-*.*s%s", screen_w - 1, screen_w - 1, " Recent files", options_base_sgr());

    for (int row = 0; row < browser_rows(); row++) {
        int idx = top + row;
        fb_gotoxy(0, 2 + row);
        if (idx < recent_count()) {
            int hl = (idx == sel);
            if (hl) fb_puts("\x1b[7m");
            snprintf(line, sizeof(line), "  %s", recent_get(idx));
            fb_printf("%-*.*s", screen_w - 1, screen_w - 1, line);
            if (hl) fb_puts(options_base_sgr());
        } else {
            fb_printf("%-*s", screen_w - 1, "");
        }
    }

    fb_gotoxy(0, screen_h - 3);
    fb_printf("\x1b[7m%-*s%s", screen_w - 1, " Enter=open  Esc=cancel  Up/Down=move", options_base_sgr());
    fb_gotoxy(0, screen_h - 2);
    fb_printf("%-*s", screen_w - 1, "");

    fb_flush();
}

void load_stream(FILE *in) {
    /* Read in chunks; a line longer than the chunk spans several reads */
    free_all_lines();
    undo_clear();
    char chunk[4096];
    int need_line = 1;
    int first_eol = -1;   /* -1 = no line ending seen yet, 1 = CRLF, 0 = LF */
    while (fgets(chunk, sizeof(chunk), in)) {
        int n = (int)strlen(chunk);
        int ends_line = (n > 0 && chunk[n - 1] == '\n');
        if (ends_line) {
            n--;
            if (first_eol < 0) first_eol = (n > 0 && chunk[n - 1] == '\r');
            if (n > 0 && chunk[n - 1] == '\r') n--;   /* CRLF files */
        }
        if (need_line) {
            insert_line_at(editor.line_count, "", 0);
            need_line = 0;
        }
        Line *ln = &editor.lines[editor.line_count - 1];
        line_reserve(ln, ln->len + n + 1);
        memcpy(ln->text + ln->len, chunk, n);
        ln->len += n;
        ln->text[ln->len] = '\0';
        if (ends_line) need_line = 1;
    }

    if (editor.line_count == 0) {
        insert_line_at(0, "", 0);
    }

    /* Keep saving in the style the file already uses */
    if (first_eol >= 0) options.crlf = first_eol;

    editor.cursor_x = 0;
    editor.cursor_y = 0;
    editor.scroll_x = 0;
    editor.scroll_y = 0;
}

/* Opens a file in a tab. A file that is already open just gets its tab
   selected. The file goes into the active tab if that is an empty untitled
   one, otherwise into a new tab. With create_missing, a name that does not
   exist yet opens as an empty file (used for command-line names). */
int open_path(const char *name, int create_missing) {
    int i = tab_find(name);
    if (i >= 0) {
        tab_switch(i);
        recent_add(name);
        return 1;
    }

    /* Binary mode keeps CR so the line ending style can be detected */
    FILE *in = fopen(name, "rb");
    if (!in && !create_missing) {
        show_message("File not found.");
        return 0;
    }

    if (!tab_is_blank() && !tab_new()) {
        if (in) fclose(in);
        show_message("Too many tabs (16 at most).");
        return 0;
    }

    options.crlf = 0;   /* a file's own line ending is detected below, else LF */
    editor.syn_override = 0;   /* a reused blank tab must not keep another file's language */
    if (in) {
        load_stream(in);
        fclose(in);
    } else {
        reset_document();
    }

    strcpy(editor.filename, name);
    tab_mark_named();
    editor.modified = 0;
    watch_record();
    if (in) recent_add(name);   /* only files that exist go in the recent list */
    return 1;
}

/* Picks a file from the recent list and opens it. */
int do_open_recent(void) {
    int sel = 0, top = 0;
    if (recent_count() == 0) {
        show_message("No recent files.");
        return 0;
    }

    while (1) {
        if (recent_count() == 0) return 0;
        if (sel >= recent_count()) sel = recent_count() - 1;
        if (sel < top) top = sel;
        if (sel >= top + browser_rows()) top = sel - browser_rows() + 1;
        draw_recent(sel, top);

        int key = read_key();
        switch (key) {
            case KEY_UP:
                if (sel > 0) sel--;
                break;
            case KEY_DOWN:
                if (sel < recent_count() - 1) sel++;
                break;
            case KEY_RESIZE:
                term_update_size();
                clear_screen();
                break;
            case KEY_ESC:
                return 0;
            case KEY_ENTER: {
                char path[512];   /* copy: opening updates the list */
                snprintf(path, sizeof(path), "%s", recent_get(sel));
                FILE *f = fopen(path, "rb");
                if (!f) {
                    recent_remove(sel);
                    show_message("File not found. Removed from recent list.");
                    break;
                }
                fclose(f);
                return open_path(path, 0);
            }
        }
    }
}

int do_open(void) {
    char name[256];
    int r = browse(0, NULL, name, sizeof(name));
    if (r == 2) return do_open_recent();
    if (r != 1) return 0;
    return open_path(name, 0);
}

/* Returns 1 if the editor should quit. Asks about each modified tab in turn;
   Esc at any prompt keeps the editor open. */
int do_exit(void) {
    int i;
    while ((i = tabs_first_modified()) >= 0) {
        tab_switch(i);
        if (!confirm_discard()) return 0;
    }
    return 1;
}
