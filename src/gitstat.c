/* Git status parser for the file tree. See gitstat.h. */
#include <stdlib.h>
#include <string.h>

#include "gitstat.h"

void gitstat_free(GitStat *g) {
    for (int i = 0; i < g->n; i++) free(g->paths[i]);
    free(g->paths);
    free(g->kinds);
    g->paths = NULL;
    g->kinds = NULL;
    g->n = g->cap = 0;
}

/* Kind from a porcelain XY code (same rules as the git panel). */
static GitKind kind_of(char x, char y) {
    if (x == 'U' || y == 'U' || (x == 'A' && y == 'A') || (x == 'D' && y == 'D')) return GK_CONFLICT;
    if (x == '?') return GK_UNTRACKED;
    if (x == 'A') return GK_NEW;
    if (x == 'D' || y == 'D') return GK_DELETED;
    if (x == 'R' || x == 'C') return GK_RENAMED;
    if (x == 'M' || y == 'M' || x == 'T' || y == 'T') return GK_MODIFIED;
    return GK_NONE;
}

static int add(GitStat *g, const char *path, size_t plen, GitKind k) {
    if (g->n >= GITSTAT_MAX) return 0;
    if (g->n == g->cap) {
        int nc = g->cap ? g->cap * 2 : 64;
        char **np = realloc(g->paths, (size_t)nc * sizeof *np);
        if (!np) return 0;
        g->paths = np;
        unsigned char *nk = realloc(g->kinds, (size_t)nc);
        if (!nk) return 0;
        g->kinds = nk;
        g->cap = nc;
    }
    char *s = malloc(plen + 1);
    if (!s) return 0;
    memcpy(s, path, plen);
    s[plen] = '\0';
    g->paths[g->n] = s;
    g->kinds[g->n] = (unsigned char)k;
    g->n++;
    return 1;
}

int gitstat_parse(GitStat *g, const char *text, size_t len, const char *prefix) {
    gitstat_free(g);
    if (!prefix) prefix = "";
    size_t plen = strlen(prefix);
    size_t pos = 0;
    while (pos < len) {
        /* One record: "XY path" up to the NUL. */
        size_t end = pos;
        while (end < len && text[end] != '\0') end++;
        const char *rec = text + pos;
        size_t rlen = end - pos;
        pos = end < len ? end + 1 : end;
        if (rlen < 4 || rec[2] != ' ') continue;

        char x = rec[0], y = rec[1];
        /* A rename or copy is followed by its old path as a separate record; skip it. */
        if (x == 'R' || x == 'C') {
            while (pos < len && text[pos] != '\0') pos++;
            if (pos < len) pos++;
        }
        GitKind k = kind_of(x, y);
        if (k == GK_NONE) continue;

        const char *path = rec + 3;
        size_t n = rlen - 3;
        if (n < plen || strncmp(path, prefix, plen) != 0) continue;
        path += plen;
        n -= plen;
        while (n > 0 && path[n - 1] == '/') n--;   /* an untracked folder is listed as "dir/" */
        if (n == 0) continue;
        if (!add(g, path, n, k)) break;
    }
    return g->n;
}

/* 1 when `child` is `dir` itself or lies under it. */
static int under(const char *child, const char *dir, size_t dlen) {
    return strncmp(child, dir, dlen) == 0 && (child[dlen] == '\0' || child[dlen] == '/');
}

GitKind gitstat_file(const GitStat *g, const char *rel) {
    GitKind best = GK_NONE;
    for (int i = 0; i < g->n; i++) {
        if (under(rel, g->paths[i], strlen(g->paths[i])) && g->kinds[i] > best)
            best = (GitKind)g->kinds[i];
    }
    return best;
}

GitKind gitstat_dir(const GitStat *g, const char *rel) {
    size_t dlen = strlen(rel);
    GitKind best = GK_NONE;
    for (int i = 0; i < g->n; i++) {
        const char *p = g->paths[i];
        if (strncmp(p, rel, dlen) == 0 && p[dlen] == '/' && g->kinds[i] > best)
            best = (GitKind)g->kinds[i];
    }
    return best;
}
