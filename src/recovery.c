/* Crash recovery: while there are unsaved changes, the document is copied to
   "<file>.recover" every BACKUP_INTERVAL seconds. A saved or discarded document
   removes the copy. If the editor dies, the next start offers to restore it. */
#include <stdio.h>
#include <string.h>
#include <time.h>

#include "editor.h"
#include "buffer.h"
#include "term.h"
#include "ui.h"
#include "options.h"
#include "files.h"
#include "tabs.h"
#include "undo.h"
#include "recovery.h"

#define BACKUP_INTERVAL 30      /* seconds between backups while changes are unsaved */
#define BACKUP_SUFFIX ".recover"
#define PATH_BUF 320            /* room for a 256-byte file name plus the suffixes */

static time_t last_backup = 0;

/* Fills path with the backup name and tmp with the name used while writing it.
   Returns 0 if the names would not fit. */
static int backup_paths(const char *name, char *path, char *tmp) {
    int n = snprintf(path, PATH_BUF, "%s%s", name, BACKUP_SUFFIX);
    if (n <= 0 || n >= PATH_BUF) return 0;
    n = snprintf(tmp, PATH_BUF, "%s.tmp", path);
    return n > 0 && n < PATH_BUF;
}

/* Writes every line of document d with the given line ending. Changes nothing in the editor. */
static void write_backup(const Editor *d, int crlf) {
    char path[PATH_BUF], tmp[PATH_BUF];
    if (!backup_paths(d->filename, path, tmp)) return;

    FILE *file = fopen(tmp, "wb");   /* binary: the line ending comes from options */
    if (!file) return;
    const char *eol = crlf ? "\r\n" : "\n";
    for (int i = 0; i < d->line_count; i++) {
        fwrite(d->lines[i].text, 1, d->lines[i].len, file);
        fputs(eol, file);
    }
    if (fclose(file) != 0) {
        remove(tmp);
        return;
    }
    /* Windows rename() does not replace an existing file, so remove the old copy first */
    remove(path);
    rename(tmp, path);
}

void recovery_tick(void) {
    int any = 0;
    for (int i = 0; i < tab_count() && !any; i++) {
        any = tab_doc(i)->modified;
    }
    if (!any) return;

    time_t now = time(NULL);
    if (last_backup != 0 && now - last_backup < BACKUP_INTERVAL) return;
    last_backup = now;
    for (int i = 0; i < tab_count(); i++) {
        if (tab_doc(i)->modified) write_backup(tab_doc(i), tab_crlf(i));
    }
}

void recovery_discard_name(const char *name) {
    char path[PATH_BUF], tmp[PATH_BUF];
    if (!backup_paths(name, path, tmp)) return;
    remove(path);
    remove(tmp);
}

/* Offers to restore the backup of the active tab, if it has one. Y restores it
   (the document stays modified); N deletes it. There is no Esc: keeping the
   backup while typing new text would overwrite it anyway.
   Returns 0 if input closed, so the caller stops asking. */
static int offer_restore(void) {
    char path[PATH_BUF], tmp[PATH_BUF];
    if (!backup_paths(editor.filename, path, tmp)) return 1;

    FILE *in = fopen(path, "rb");
    if (!in) in = fopen(tmp, "rb");   /* a crash mid-write leaves only the .tmp copy */
    if (!in) return 1;

    /* Keep the question whole: the file name gets whatever room is left */
    int room = screen_w - 44;
    if (room < 0) room = 0;
    char question[PATH_BUF];
    snprintf(question, sizeof(question), "Unsaved changes in %.*s. Recover? (Y)es / (N)o",
             room, editor.filename);
    gotoxy(0, screen_h - 2);
    printf("%-*.*s", screen_w - 1, screen_w - 1, question);
    fflush(stdout);

    while (1) {
        int c = getch_prompt();
        if (c == 'y' || c == 'Y') {
            load_stream(in);
            fclose(in);
            editor.modified = 1;
            undo_mark_unsaved();
            return 1;
        }
        if (c == 'n' || c == 'N') {
            fclose(in);
            recovery_discard_name(editor.filename);
            return 1;
        }
        if (c == -1) {          /* input closed: decide nothing, keep the backup */
            fclose(in);
            return 0;
        }
    }
}

/* Offers to restore a backup for each open tab that has a file name. Runs
   from the main loop start-up, so the tab it was on is switched back afterwards. */
void recovery_check_on_startup(void) {
    int start = tab_current();
    for (int i = 0; i < tab_count(); i++) {
        if (!tab_named(i)) continue;   /* untitled tabs have no backup */
        tab_switch(i);
        if (!offer_restore()) break;
    }
    tab_switch(start);
}
