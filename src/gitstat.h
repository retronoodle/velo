/* Pure parser for `git status --porcelain=v1 -z`, for coloring the file tree. No I/O. */
#ifndef GITSTAT_H
#define GITSTAT_H

#include <stddef.h>

#define GITSTAT_MAX 20000   /* entries kept */

/* Ascending severity: a folder shows the highest kind found beneath it.
   Deleted ranks below Modified on purpose: a folder with edits reads as modified. */
typedef enum {
    GK_NONE = 0, GK_UNTRACKED, GK_NEW, GK_RENAMED, GK_DELETED, GK_MODIFIED, GK_CONFLICT, GK_COUNT
} GitKind;

typedef struct {
    char **paths;            /* relative to the tree root, '/' separated, no trailing '/' */
    unsigned char *kinds;
    int n, cap;
} GitStat;

void gitstat_free(GitStat *g);

/* Parses `git status --porcelain=v1 -z` output (len bytes) into g, replacing its contents.
   Paths are repo-relative; `prefix` (git's --show-prefix, "" or "sub/dir/") is stripped and
   entries outside it are dropped. Returns the number of entries kept. */
int gitstat_parse(GitStat *g, const char *text, size_t len, const char *prefix);

/* Kind of one path: an exact entry, or a path inside an untracked folder git listed whole. */
GitKind gitstat_file(const GitStat *g, const char *rel);

/* Most severe kind of any entry under folder `rel`. */
GitKind gitstat_dir(const GitStat *g, const char *rel);

#endif
