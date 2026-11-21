/* Unit tests for gitstat.c. Build and run with: make test */
#include <stdio.h>
#include <string.h>
#include "gitstat.h"

static int failures = 0;

#define CHECK(cond, name) do { \
    if (cond) printf("ok   %s\n", name); \
    else { printf("FAIL %s\n", name); failures++; } \
} while (0)

#define PARSE(g, s, prefix) gitstat_parse(&(g), (s), sizeof(s) - 1, (prefix))

int main(void) {
    GitStat g = {0};

    /* Plain kinds; -z records are NUL separated */
    static const char basic[] =
        " M src/a.c\0M  src/b.c\0A  new.txt\0 D gone.txt\0?? scratch.txt\0UU clash.c\0";
    CHECK(PARSE(g, basic, "") == 6, "six entries");
    CHECK(gitstat_file(&g, "src/a.c") == GK_MODIFIED, "worktree modified");
    CHECK(gitstat_file(&g, "src/b.c") == GK_MODIFIED, "staged modified");
    CHECK(gitstat_file(&g, "new.txt") == GK_NEW, "added");
    CHECK(gitstat_file(&g, "gone.txt") == GK_DELETED, "deleted");
    CHECK(gitstat_file(&g, "scratch.txt") == GK_UNTRACKED, "untracked");
    CHECK(gitstat_file(&g, "clash.c") == GK_CONFLICT, "conflict");
    CHECK(gitstat_file(&g, "src/c.c") == GK_NONE, "clean file");
    CHECK(gitstat_file(&g, "src") == GK_NONE, "file lookup does not roll up");

    /* Folders take the most severe kind beneath them */
    static const char roll[] = "?? d/new.txt\0 M d/sub/m.c\0A  e/f.c\0";
    PARSE(g, roll, "");
    CHECK(gitstat_dir(&g, "d") == GK_MODIFIED, "folder: modified beats untracked");
    CHECK(gitstat_dir(&g, "d/sub") == GK_MODIFIED, "nested folder");
    CHECK(gitstat_dir(&g, "e") == GK_NEW, "folder: added");
    CHECK(gitstat_dir(&g, "x") == GK_NONE, "folder: clean");
    CHECK(gitstat_dir(&g, "d/su") == GK_NONE, "folder: no partial-name match");
    static const char conf[] = " M d/a\0UU d/b\0";
    PARSE(g, conf, "");
    CHECK(gitstat_dir(&g, "d") == GK_CONFLICT, "folder: conflict beats modified");

    /* A rename lists the old path as its own record; it must not become an entry */
    static const char ren[] = "R  new.c\0old.c\0 M other.c\0";
    CHECK(PARSE(g, ren, "") == 2, "rename: two entries");
    CHECK(gitstat_file(&g, "new.c") == GK_RENAMED, "rename: new path");
    CHECK(gitstat_file(&g, "old.c") == GK_NONE, "rename: old path ignored");
    CHECK(gitstat_file(&g, "other.c") == GK_MODIFIED, "rename: next record still read");

    /* An untracked folder listed whole colors everything inside it */
    static const char ut[] = "?? vendor/\0";
    PARSE(g, ut, "");
    CHECK(gitstat_file(&g, "vendor") == GK_UNTRACKED, "untracked folder itself");
    CHECK(gitstat_file(&g, "vendor/x/y.php") == GK_UNTRACKED, "file inside untracked folder");
    CHECK(gitstat_file(&g, "vendors") == GK_NONE, "untracked folder: no partial-name match");

    /* Tree root is a subfolder: strip git's prefix, drop entries outside it */
    static const char sub[] = " M app/src/a.c\0?? app/n.txt\0 M other/z.c\0";
    CHECK(PARSE(g, sub, "app/") == 2, "prefix: outside entries dropped");
    CHECK(gitstat_file(&g, "src/a.c") == GK_MODIFIED, "prefix: stripped path");
    CHECK(gitstat_dir(&g, "src") == GK_MODIFIED, "prefix: folder rollup");
    CHECK(gitstat_file(&g, "other/z.c") == GK_NONE, "prefix: outside path");

    /* Junk and empty input */
    CHECK(gitstat_parse(&g, "", 0, "") == 0, "empty input");
    static const char junk[] = "x\0fatal: not a git repository\0!! ignored.log\0";
    CHECK(PARSE(g, junk, "") == 0, "junk and ignored files skipped");

    gitstat_free(&g);
    if (failures) {
        printf("%d test(s) failed\n", failures);
        return 1;
    }
    printf("All gitstat tests passed\n");
    return 0;
}
