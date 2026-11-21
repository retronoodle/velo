/* File tree sidebar. See sidebar.h. */
#ifndef _WIN32
    #define _XOPEN_SOURCE 700   /* realpath under -std=c99 */
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "editor.h"
#include "aichat.h"
#include "fileops.h"
#include "filetree.h"
#include "files.h"
#include "gitstat.h"
#include "menu.h"
#include "options.h"
#include "sidebar.h"
#include "term.h"
#include "ui.h"
#include "utf8.h"

#ifdef _WIN32
    #include <direct.h>
    #include <windows.h>
#else
    #include <dirent.h>
    #include <sys/stat.h>
    #include <sys/wait.h>
    #include <unistd.h>
#endif

#define PATH_LEN 1024
#define MIN_SIDE_W 22
#define MAX_SIDE_W 32
#define MIN_REST_W 40          /* columns that must stay for the text (and the chat) */
#define DOUBLE_CLICK_MS 400

static int is_open = 0, has_focus = 0;
static char root[PATH_LEN];
static FileTree tree;
static int tree_ready = 0;
static int sel = 0, top = 0;
static int hide_noise = 0;           /* h: hide .git, node_modules, build, vendor */
static char clip[PATH_LEN];          /* full path of the copied file or folder */
static int has_clip = 0;

static int popup = 0, popup_sel = 0, popup_x = 0, popup_y = 0;
static const char *const popup_items[] = {"New File", "New Folder", "Copy", "Paste", "Refresh"};
#define POPUP_N 5
#define POPUP_W 14             /* box width, borders included */

static int click_pending = 0, click_row = -1;
static unsigned long click_ms = 0;

static GitStat gstat;                /* git status of the tree's folder, for coloring rows */
static int git_stale = 1;            /* reload on the next draw (the tree changed) */
static unsigned long git_next_ms = 0;
static unsigned long git_last_ms = 0;   /* when load_git last ran */
#define GIT_EVERY_MS 3000

/* ---- Disk access ---- */

void sidebar_set_root(void) {
#ifdef _WIN32
    char cwd[PATH_LEN];
    if (_getcwd(cwd, sizeof cwd) && _fullpath(root, cwd, sizeof root)) {
        for (char *p = root; *p; p++) if (*p == '\\') *p = '/';
        size_t n = strlen(root);
        if (n > 3 && root[n - 1] == '/') root[n - 1] = '\0';   /* keep "C:/" intact */
        return;
    }
#else
    char cwd[PATH_LEN];
    if (getcwd(cwd, sizeof cwd) && realpath(cwd, root)) return;
#endif
    root[0] = '\0';
}

static int is_noise(const char *name) {
    return !strcmp(name, ".git") || !strcmp(name, "node_modules") ||
           !strcmp(name, "build") || !strcmp(name, "vendor");
}

static void full_path(char *out, size_t size, const char *rel) {
    const char *base = root[0] ? root : ".";
    if (rel[0]) snprintf(out, size, "%s/%s", base, rel);
    else snprintf(out, size, "%s", base);
}

static int list_dir(const char *rel, FtEntry **out, int *n, void *ctx) {
    (void)ctx;
    *out = NULL;
    *n = 0;
    char path[PATH_LEN];
    full_path(path, sizeof path, rel);
#ifdef _WIN32
    char pattern[PATH_LEN];
    if (snprintf(pattern, sizeof pattern, "%s/*", path) >= (int)sizeof pattern) return -1;
    WIN32_FIND_DATAA fd;
    HANDLE h = FindFirstFileA(pattern, &fd);
    if (h == INVALID_HANDLE_VALUE) return -1;
    int cap = 0;
    do {
        const char *nm = fd.cFileName;
        if (!strcmp(nm, ".") || !strcmp(nm, "..") || strlen(nm) >= sizeof((*out)->name)) continue;
        if (hide_noise && is_noise(nm)) continue;
        if (*n >= FT_MAX_CHILDREN) break;
        if (*n == cap) {
            cap = cap ? cap * 2 : 32;
            FtEntry *g = realloc(*out, (size_t)cap * sizeof **out);
            if (!g) break;
            *out = g;
        }
        FtEntry *en = &(*out)[(*n)++];
        strcpy(en->name, nm);
        en->is_dir = (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
    } while (FindNextFileA(h, &fd));
    FindClose(h);
    return 0;
#else
    DIR *d = opendir(path);
    if (!d) return -1;
    int cap = 0;
    struct dirent *e;
    while ((e = readdir(d))) {
        const char *nm = e->d_name;
        if (!strcmp(nm, ".") || !strcmp(nm, "..") || strlen(nm) >= sizeof((*out)->name)) continue;
        if (hide_noise && is_noise(nm)) continue;
        if (*n >= FT_MAX_CHILDREN) break;
        if (*n == cap) {
            cap = cap ? cap * 2 : 32;
            FtEntry *g = realloc(*out, (size_t)cap * sizeof **out);
            if (!g) break;
            *out = g;
        }
        char child[PATH_LEN];
        int len = snprintf(child, sizeof child, "%s/%s", path, nm);
        struct stat st;
        FtEntry *en = &(*out)[(*n)++];
        strcpy(en->name, nm);
        en->is_dir = len > 0 && (size_t)len < sizeof child && stat(child, &st) == 0 && S_ISDIR(st.st_mode);
    }
    closedir(d);
    return 0;
#endif
}

static void ensure_tree(void) {
    if (tree_ready) return;
    ft_init(&tree, list_dir, NULL);
    tree_ready = 1;
    sel = top = 0;
}

/* Re-reads the disk and keeps the selected row when it is still there. */
static void refresh_tree(void);

/* Cheap fingerprint of the folders on screen: the root and every expanded folder's mtime.
   A file created or removed in one of them changes it. */
static unsigned long long dirs_stamp(void) {
    unsigned long long sum = 0;
    for (int i = -1; i < tree.n; i++) {
        if (i >= 0 && !(tree.rows[i].is_dir && tree.rows[i].expanded)) continue;
        char path[PATH_LEN];
        full_path(path, sizeof path, i < 0 ? "" : tree.rows[i].rel);
#ifdef _WIN32
        WIN32_FILE_ATTRIBUTE_DATA fa;
        if (!GetFileAttributesExA(path, GetFileExInfoStandard, &fa)) continue;
        sum = sum * 31 + (((unsigned long long)fa.ftLastWriteTime.dwHighDateTime << 32) |
                          fa.ftLastWriteTime.dwLowDateTime);
#else
        struct stat st;
        if (stat(path, &st) != 0) continue;
        sum = sum * 31 + (unsigned long long)st.st_mtim.tv_sec * 1000000007ULL +
              (unsigned long long)st.st_mtim.tv_nsec;
#endif
    }
    return sum;
}

/* ---- Git status colors ---- */

/* Reads all of a command's stdout (NUL bytes kept). Returns a malloc'd buffer, or NULL when the
   command fails to run or exits non-zero. */
static char *read_cmd(const char *cmd, size_t *len) {
#ifdef _WIN32
    FILE *p = _popen(cmd, "rb");
#else
    FILE *p = popen(cmd, "r");
#endif
    if (!p) return NULL;
    size_t cap = 4096, n = 0;
    char *buf = malloc(cap);
    size_t got;
    while (buf && (got = fread(buf + n, 1, cap - n - 1, p)) > 0) {
        n += got;
        if (n + 1 >= cap) {
            char *nb = realloc(buf, cap * 2);
            if (!nb) { free(buf); buf = NULL; break; }
            buf = nb;
            cap *= 2;
        }
    }
#ifdef _WIN32
    int rc = _pclose(p);
#else
    int rc = pclose(p);
    rc = (WIFEXITED(rc) && WEXITSTATUS(rc) == 0) ? 0 : -1;
#endif
    if (!buf) return NULL;
    if (rc != 0) { free(buf); return NULL; }
    buf[n] = '\0';
    *len = n;
    return buf;
}

/* Quotes `s` for the shell. Returns 0 when it does not fit. */
static int shell_quote(char *dst, size_t size, const char *s) {
#ifdef _WIN32
    const char q = '"';
    if (strchr(s, '"') || strchr(s, '%')) return 0;
#else
    const char q = '\'';
#endif
#ifdef _WIN32
    const char *s0 = s;
#endif
    size_t n = 0;
    if (n + 1 >= size) return 0;
    dst[n++] = q;
    for (; *s; s++) {
#ifndef _WIN32
        if (*s == '\'') {
            if (n + 4 >= size) return 0;
            memcpy(dst + n, "'\\''", 4);
            n += 4;
            continue;
        }
#endif
        if (n + 2 >= size) return 0;
        dst[n++] = *s;
    }
#ifdef _WIN32
    /* Backslashes right before the closing quote would escape it: double them. */
    for (const char *e = s; e > s0 && e[-1] == '\\'; e--) {
        if (n + 2 >= size) return 0;
        dst[n++] = '\\';
    }
#endif
    dst[n++] = q;
    dst[n] = '\0';
    return 1;
}

/* Re-reads `git status` for the root folder. Not a repo, or no git: nothing gets colored. */
static void load_git(void) {
    git_stale = 0;
    unsigned long t0 = term_ticks_ms();
    git_last_ms = t0;
    char q[PATH_LEN * 2 + 8], cmd[PATH_LEN * 2 + 128];
    if (!root[0] || !shell_quote(q, sizeof q, root)) { gitstat_free(&gstat); return; }
#ifdef _WIN32
    const char *quiet = "2>nul";
#else
    const char *quiet = "2>/dev/null";
#endif
    size_t len = 0;
    snprintf(cmd, sizeof cmd, "git -C %s rev-parse --show-prefix %s", q, quiet);
    char *pre = read_cmd(cmd, &len);
    if (!pre) { gitstat_free(&gstat); git_next_ms = t0 + GIT_EVERY_MS; return; }
    while (len > 0 && (pre[len - 1] == '\n' || pre[len - 1] == '\r')) pre[--len] = '\0';

    snprintf(cmd, sizeof cmd, "git --no-optional-locks -C %s status --porcelain=v1 -z %s", q, quiet);
    char *out = read_cmd(cmd, &len);
    if (out) gitstat_parse(&gstat, out, len, pre);
    else gitstat_free(&gstat);
    free(out);
    free(pre);

    /* A slow repo is polled less often, so the UI never spends most of its time in git. */
    unsigned long took = term_ticks_ms() - t0;
    git_next_ms = term_ticks_ms() + (took * 10 > GIT_EVERY_MS ? took * 10 : GIT_EVERY_MS);
}

/* Picks up files created or deleted outside the tree (saves, chat writes, other programs). */
static void auto_refresh(void) {
    static unsigned long last_ms = 0;
    static unsigned long long stamp = 0;
    unsigned long now = term_ticks_ms();
    if (now - last_ms < 1000) return;
    last_ms = now;
    unsigned long long cur = dirs_stamp();
    if (cur != stamp) {
        if (stamp) refresh_tree();
        stamp = dirs_stamp();   /* re-read: the refresh may have changed the expanded set */
    }
}

static void refresh_tree(void) {
    if (!tree_ready) return;
    git_stale = 1;
    char keep[PATH_LEN] = "";
    if (sel >= 0 && sel < tree.n) snprintf(keep, sizeof keep, "%s", tree.rows[sel].rel);
    ft_refresh(&tree);
    int i = keep[0] ? ft_find(&tree, keep) : -1;
    sel = i >= 0 ? i : (sel < tree.n ? sel : 0);
}

/* Length of `root` plus its separator when `path` lies under it, else 0. A root that already
   ends in '/' (a drive root like "C:/" or "/") needs no extra separator. */
static size_t under_root(const char *path, int icase) {
    size_t n = strlen(root);
    if (!n) return 0;
#ifdef _WIN32
    if (icase ? _strnicmp(path, root, n) : strncmp(path, root, n)) return 0;
#else
    (void)icase;
    if (strncmp(path, root, n) != 0) return 0;
#endif
    if (root[n - 1] == '/') return path[n] ? n : 0;
    return path[n] == '/' && path[n + 1] ? n + 1 : 0;
}

/* Path of the active document relative to the root ("" if it is not under the root). */
static const char *active_rel(char *buf, size_t size) {
    char abs[PATH_LEN];
#ifdef _WIN32
    if (!editor.filename[0] || !root[0] || !_fullpath(abs, editor.filename, sizeof abs)) return "";
    for (char *p = abs; *p; p++) if (*p == '\\') *p = '/';
    size_t n = under_root(abs, 1);
    if (!n) return "";
#else
    if (!editor.filename[0] || !root[0] || !realpath(editor.filename, abs)) return "";
    size_t n = under_root(abs, 0);
    if (!n) return "";
#endif
    snprintf(buf, size, "%s", abs + n);
    return buf;
}

/* ---- Selection and scrolling ---- */

static int view_rows(void) {
    int r = TEXT_ROWS - 2;   /* header and hint rows */
    return r < 1 ? 1 : r;
}

/* Keeps sel and top inside the tree (does not move the view toward sel, so the wheel can
   scroll away from the selection). */
static void clamp_view(void) {
    if (!tree_ready) return;
    int vr = view_rows();
    if (sel >= tree.n) sel = tree.n - 1;
    if (sel < 0) sel = 0;
    int max_top = tree.n - vr;
    if (top > max_top) top = max_top;
    if (top < 0) top = 0;
}

/* Scrolls so the selected row is visible: call after the selection or the rows change. */
static void show_sel(void) {
    if (!tree_ready) return;
    int vr = view_rows();
    clamp_view();
    if (sel < top) top = sel;
    if (sel >= top + vr) top = sel - vr + 1;
    clamp_view();
}

static void select_row(int i) {
    sel = i;
    show_sel();
}

/* ---- Layout ---- */

int sidebar_width(void) {
    if (!is_open) return 0;
    int w = screen_w / 4;
    if (w < MIN_SIDE_W) w = MIN_SIDE_W;
    if (w > MAX_SIDE_W) w = MAX_SIDE_W;
    if (screen_w - w - aichat_width() < MIN_REST_W) return 0;
    return w;
}

int sidebar_focused(void) {
    return has_focus && sidebar_width() > 0;
}

void sidebar_blur(void) {
    has_focus = 0;
    popup = 0;
}

static void reveal_active(void) {
    char buf[PATH_LEN];
    const char *rel = active_rel(buf, sizeof buf);
    if (!*rel) return;
    int i = ft_reveal(&tree, rel);
    if (i >= 0) select_row(i);
}

void sidebar_toggle(void) {
    if (!is_open) {
        is_open = 1;
        if (sidebar_width() == 0) {
            is_open = 0;
            show_message("Not enough room for the file tree: make the window wider.");
            return;
        }
        ensure_tree();
        refresh_tree();
        reveal_active();
        has_focus = 1;
        aichat_unfocus();
    } else if (!has_focus) {
        has_focus = 1;
        aichat_unfocus();
    } else {
        is_open = 0;
        has_focus = 0;
        popup = 0;
    }
}

/* ---- Actions ---- */

/* Folder (relative path) that new files and pastes go into: the selected folder, or the
   folder of the selected file. */
static void target_rel(char *out, size_t size) {
    out[0] = '\0';
    if (!tree_ready || sel < 0 || sel >= tree.n) return;
    const FtRow *r = &tree.rows[sel];
    if (r->is_dir) {
        snprintf(out, size, "%s", r->rel);
    } else {
        int p = ft_parent(&tree, sel);
        if (p >= 0) snprintf(out, size, "%s", tree.rows[p].rel);
    }
}

static void open_file_at(const char *rel) {
    char path[PATH_LEN];
    full_path(path, sizeof path, rel);
    if (strlen(path) >= sizeof editor.filename) {
        show_message("That path is too long to open.");
        return;
    }
    if (open_path(path, 0)) has_focus = 0;
}

static void open_row(int i) {
    if (i < 0 || i >= tree.n) return;
    if (tree.rows[i].is_dir) {
        ft_toggle(&tree, i);
        show_sel();
    } else {
        char rel[PATH_LEN];
        snprintf(rel, sizeof rel, "%s", tree.rows[i].rel);
        open_file_at(rel);
    }
}

/* Selects `path` (absolute) after a refresh, opening the folders above it. */
static void select_path(const char *path) {
    size_t n = under_root(path, 0);
    if (!n) return;
    int i = ft_reveal(&tree, path + n);
    if (i >= 0) select_row(i);
}

static void new_item(int folder) {
    if (!tree_ready) return;
    char name[256], dir_rel[PATH_LEN], dir[PATH_LEN], out[PATH_LEN];
    if (!prompt_line(folder ? "New folder name: " : "New file name: ", name, sizeof name)) return;
    target_rel(dir_rel, sizeof dir_rel);
    full_path(dir, sizeof dir, dir_rel);
    const char *err = folder ? fileops_new_dir(dir, name, out, sizeof out)
                             : fileops_new_file(dir, name, out, sizeof out);
    if (err) {
        show_message(err);
        return;
    }
    refresh_tree();
    select_path(out);
    if (!folder) open_file_at(out + strlen(root) + 1);
}

static void do_copy(void) {
    if (!tree_ready || sel < 0 || sel >= tree.n) return;
    full_path(clip, sizeof clip, tree.rows[sel].rel);
    has_clip = 1;
    char msg[300];
    snprintf(msg, sizeof msg, "Copied %s.", tree.rows[sel].name);
    show_message(msg);
}

static void do_paste(void) {
    if (!has_clip) {
        show_message("Nothing copied yet.");
        return;
    }
    char dir_rel[PATH_LEN], dir[PATH_LEN], out[PATH_LEN];
    target_rel(dir_rel, sizeof dir_rel);
    full_path(dir, sizeof dir, dir_rel);
    const char *err = fileops_copy(clip, dir, out, sizeof out);
    if (err) {
        show_message(err);
        return;
    }
    refresh_tree();
    select_path(out);
}

static void run_item(int item) {
    popup = 0;
    switch (item) {
        case 0: new_item(0); break;
        case 1: new_item(1); break;
        case 2: do_copy(); break;
        case 3: do_paste(); break;
        case 4: refresh_tree(); break;
    }
}

/* ---- Drawing ---- */

/* Writes s as exactly cw cells: clipped at a character boundary, padded with spaces.
   Control bytes show as '?'. */
static void put_cells(const char *s, int len, int cw) {
    int col = 0, pos = 0;
    while (pos < len && col < cw) {
        int nxt = utf8_next(s, len, pos);
        int cp;
        utf8_decode(s + pos, len - pos, &cp);
        if (cp < 32 || cp == 127) {
            fb_write("?", 1);
            col++;
        } else {
            int w = utf8_cell_w(cp, col);
            if (col + w > cw) break;
            fb_write(s + pos, nxt - pos);
            col += w;
        }
        pos = nxt;
    }
    for (; col < cw; col++) fb_write(" ", 1);
}

/* Columns before the name: indent, then "[+] " / "[-] " for folders (4 blanks for files). */
static int marker_col(const FtRow *r) {
    return r->depth * 2;
}

static void draw_popup(int w) {
    int x = popup_x, y = popup_y;
    if (x + POPUP_W > w) x = w - POPUP_W;
    if (x < 0) x = 0;
    popup_x = x;
    const char *dim = options_chrome_sgr(CH_DIM);
    const char *selc = options_chrome_sgr(CH_DROP_SEL);
    fb_gotoxy(x, y);
    fb_puts(dim);
    fb_puts("\xe2\x94\x8c");
    for (int k = 0; k < POPUP_W - 2; k++) fb_puts("\xe2\x94\x80");
    fb_puts("\xe2\x94\x90");
    for (int i = 0; i < POPUP_N; i++) {
        fb_gotoxy(x, y + 1 + i);
        fb_puts(options_base_sgr());
        fb_puts(dim);
        fb_puts("\xe2\x94\x82");
        fb_puts(i == popup_sel ? selc : options_base_sgr());
        fb_printf(" %-*s ", POPUP_W - 4, popup_items[i]);
        fb_puts(dim);
        fb_puts("\xe2\x94\x82");
    }
    fb_gotoxy(x, y + 1 + POPUP_N);
    fb_puts("\xe2\x94\x94");
    for (int k = 0; k < POPUP_W - 2; k++) fb_puts("\xe2\x94\x80");
    fb_puts("\xe2\x94\x98");
    fb_puts(options_base_sgr());
}

void sidebar_draw(void) {
    int w = sidebar_width();
    if (!w) return;
    ensure_tree();
    auto_refresh();
    unsigned long gnow = term_ticks_ms();
    /* A changed tree reloads sooner, but never more than twice a second. */
    if (gnow >= git_next_ms || (git_stale && gnow - git_last_ms >= 500)) load_git();
    clamp_view();
    int cw = w - 1;
    int vr = view_rows();
    char act_rel_buf[PATH_LEN];
    const char *act = active_rel(act_rel_buf, sizeof act_rel_buf);

    /* Header: the folder name */
    const char *name = strrchr(root, '/');
    name = (name && name[1]) ? name + 1 : (root[0] ? root : ".");
    char head[PATH_LEN + 4];
    snprintf(head, sizeof head, " %s", name);
    fb_gotoxy(0, TEXT_TOP);
    fb_puts(options_chrome_sgr(CH_BAR));
    put_cells(head, (int)strlen(head), cw);
    fb_puts(options_chrome_sgr(CH_DIM));
    fb_puts("\xe2\x94\x82");

    for (int r = 0; r < vr; r++) {
        int i = top + r;
        fb_gotoxy(0, TEXT_TOP + 1 + r);
        fb_puts(options_base_sgr());
        if (i < tree.n) {
            const FtRow *row = &tree.rows[i];
            char line[PATH_LEN + 16];
            int n = snprintf(line, sizeof line, "%*s", marker_col(row), "");
            n += snprintf(line + n, sizeof line - (size_t)n, "%s%s",
                          row->is_dir ? (row->expanded ? "[-] " : "[+] ") : "    ", row->name);
            if (i == sel) fb_puts(has_focus ? options_chrome_sgr(CH_DROP_SEL) : "\x1b[4m");
            else if (*act && strcmp(row->rel, act) == 0) fb_puts("\x1b[1m");
            else if (row->is_dir) fb_puts("\x1b[1m");
            if (i != sel || !has_focus) {
                GitKind gk = gitstat_file(&gstat, row->rel);
                if (row->is_dir) {
                    GitKind in = gitstat_dir(&gstat, row->rel);
                    if (in > gk) gk = in;
                }
                fb_puts(options_git_sgr(gk));
            }
            put_cells(line, n, cw);
        } else {
            put_cells("", 0, cw);
        }
        fb_puts(options_chrome_sgr(CH_DIM));
        fb_puts("\xe2\x94\x82");
    }

    /* Hint row */
    const char *hint = has_focus ? "Enter open  n/N new  c/v copy/paste" : "Alt+B to focus";
    fb_gotoxy(0, screen_h - 3);
    fb_puts(options_chrome_sgr(CH_DIM));
    put_cells(hint, (int)strlen(hint), cw);
    fb_puts("\xe2\x94\x82");
    fb_puts(options_base_sgr());

    if (popup) draw_popup(w);
}

int sidebar_cursor(int *x, int *y) {
    if (!sidebar_focused() || !tree_ready || tree.n == 0) return 0;
    clamp_view();
    if (sel < top || sel >= top + view_rows()) return 0;   /* scrolled away by the wheel */
    *x = marker_col(&tree.rows[sel]);
    *y = TEXT_TOP + 1 + (sel - top);
    return 1;
}

/* ---- Keys ---- */

static void open_popup(int x, int y) {
    popup = 1;
    popup_sel = 0;
    popup_x = x;
    int h = POPUP_N + 2;
    popup_y = y;
    if (popup_y + h > screen_h - 2) popup_y = screen_h - 2 - h;
    if (popup_y < TEXT_TOP) popup_y = TEXT_TOP;
}

static int popup_key(int key) {
    switch (key) {
        case KEY_UP:    popup_sel = (popup_sel + POPUP_N - 1) % POPUP_N; break;
        case KEY_DOWN:  popup_sel = (popup_sel + 1) % POPUP_N; break;
        case KEY_ENTER: run_item(popup_sel); break;
        case KEY_ESC:   popup = 0; break;
        case KEY_CTRL_Q: case KEY_CTRL_S: case KEY_F6: case KEY_ALT_B:
            popup = 0;
            return 0;   /* not ours: close the popup and let the editor handle it */
        default: break;
    }
    return 1;
}

int sidebar_key(int key) {
    ensure_tree();
    if (popup) return popup_key(key);
    int n = tree.n;
    switch (key) {
    case KEY_ALT_B: sidebar_toggle(); return 1;
    case KEY_ESC:   has_focus = 0; return 1;
    case KEY_UP:    select_row(sel > 0 ? sel - 1 : 0); return 1;
    case KEY_DOWN:  select_row(sel + 1 < n ? sel + 1 : sel); return 1;
    case KEY_RIGHT:
        if (n && tree.rows[sel].is_dir) {
            if (!tree.rows[sel].expanded) ft_expand(&tree, sel);
            else if (sel + 1 < n) sel++;
            show_sel();
        }
        return 1;
    case KEY_LEFT:
        if (n) {
            if (tree.rows[sel].is_dir && tree.rows[sel].expanded) ft_collapse(&tree, sel);
            else {
                int p = ft_parent(&tree, sel);
                if (p >= 0) sel = p;
            }
            show_sel();
        }
        return 1;
    case KEY_ENTER: open_row(sel); return 1;
    case KEY_CTRL_C: do_copy(); return 1;
    case KEY_CTRL_V: do_paste(); return 1;
    /* Editor-only commands must not act on the document while the sidebar has focus. */
    case KEY_BACKSPACE: case KEY_DELETE: case KEY_TAB: case KEY_INSERT:
    case KEY_CTRL_X: case KEY_CTRL_A: case KEY_CTRL_Y:
    case KEY_SHIFT_UP: case KEY_SHIFT_DOWN: case KEY_SHIFT_LEFT: case KEY_SHIFT_RIGHT:
    case KEY_BLOCK_UP: case KEY_BLOCK_DOWN: case KEY_BLOCK_LEFT: case KEY_BLOCK_RIGHT:
        return 1;
    default:
        if (key >= 32 && key < KEY_BASE) {
            switch (key) {
            case '+': case '=': if (n) { ft_expand(&tree, sel); show_sel(); } break;
            case '-': if (n) { ft_collapse(&tree, sel); show_sel(); } break;
            case 'n': new_item(0); break;
            case 'N': new_item(1); break;
            case 'c': do_copy(); break;
            case 'v': do_paste(); break;
            case 'r': refresh_tree(); break;
            case 'h':
                hide_noise = !hide_noise;
                refresh_tree();
                show_message(hide_noise ? "Hiding .git, node_modules, build and vendor."
                                        : "Showing every file and folder.");
                break;
            default: break;
            }
            return 1;
        }
        return 0;
    }
}

/* ---- Mouse ---- */

int sidebar_mouse(const MouseEvent *ev) {
    int w = sidebar_width();
    if (!w || editor.menu_active) return 0;
    int in_pane = ev->x < w && ev->y >= TEXT_TOP && ev->y <= screen_h - 3;
    if (!in_pane) {
        if (ev->kind == MEV_PRESS || ev->kind == MEV_RIGHT_PRESS) sidebar_blur();
        return 0;
    }
    ensure_tree();

    if (popup) {   /* every event is the popup's while it is open */
        if (ev->kind == MEV_PRESS) {
            int item = ev->y - popup_y - 1;
            if (ev->x >= popup_x && ev->x < popup_x + POPUP_W && item >= 0 && item < POPUP_N) run_item(item);
            else popup = 0;
        } else if (ev->kind == MEV_RIGHT_PRESS) {
            popup = 0;
        }
        return 1;
    }

    int row = ev->y - (TEXT_TOP + 1) + top;
    int on_row = ev->y > TEXT_TOP && ev->y < screen_h - 3 && row >= 0 && row < tree.n;

    switch (ev->kind) {
    case MEV_WHEEL_UP:
        top -= 3;
        if (top < 0) top = 0;
        return 1;
    case MEV_WHEEL_DOWN: {
        int max_top = tree.n - view_rows();
        top += 3;
        if (top > max_top) top = max_top;
        if (top < 0) top = 0;
        return 1;
    }
    case MEV_PRESS: {
        has_focus = 1;
        aichat_unfocus();
        if (!on_row) { click_pending = 0; return 1; }
        unsigned long now = term_ticks_ms();
        int dbl = click_pending && click_row == row && now - click_ms <= DOUBLE_CLICK_MS;
        click_pending = !dbl;
        click_row = row;
        click_ms = now;
        const FtRow *r = &tree.rows[row];
        select_row(row);
        if (dbl) open_row(row);
        else if (r->is_dir && ev->x >= marker_col(r) && ev->x < marker_col(r) + 3) {
            ft_toggle(&tree, row);   /* a click on [+] / [-] */
            show_sel();
            click_pending = 0;
        }
        return 1;
    }
    case MEV_RIGHT_PRESS:
        has_focus = 1;
        aichat_unfocus();
        if (on_row) select_row(row);
        open_popup(ev->x, ev->y);
        return 1;
    default:
        return 0;   /* drag/release belong to a selection that started in the editor */
    }
}
