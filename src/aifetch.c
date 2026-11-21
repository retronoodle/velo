/* The network side of the chat's fetch_url tool. The URL goes into a 0600 curl config file, not
   onto the command line, so it never meets a shell. curl does not follow redirects: each hop
   (up to five) is checked again with aiweb_url_ok, and on POSIX the host is resolved here, every
   address must be public, and curl is pinned to one of them (--resolve) so a DNS answer cannot
   change between the check and the request. */
#ifndef _WIN32
    #define _POSIX_C_SOURCE 200809L
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifndef _WIN32
    #include <arpa/inet.h>
    #include <netdb.h>
    #include <sys/socket.h>
#endif

#include "ai.h"
#include "aiask.h"
#include "aifetch.h"
#include "aiweb.h"

#define FETCH_TIMEOUT_S 20
#define FETCH_MAX_BYTES (2 * 1024 * 1024)
#define FETCH_MAX_HOPS 5

static char *err_str(const char *msg) {
    char tmp[300];
    snprintf(tmp, sizeof tmp, "Error: %s", msg);
    return strdup(tmp);
}

static int ci_contains(const char *s, const char *needle) {
    size_t nl = strlen(needle);
    for (; *s; s++) {
        size_t i = 0;
        while (i < nl && s[i] && ((s[i] | 32) == needle[i])) i++;
        if (i == nl) return 1;
    }
    return 0;
}

#ifndef _WIN32
/* Resolves the URL's host. Fails if any address is not public. On success pin (size n) holds a
   curl --resolve value "host:port:ip" for the first address. */
static int resolve_public(const char *url, char *pin, size_t n, char *why, size_t wn) {
    char host[256];
    int port = 0;
    pin[0] = '\0';
    if (!aiweb_url_host(url, host, sizeof host, &port)) { snprintf(why, wn, "bad URL"); return 0; }
    struct addrinfo hints, *res = NULL;
    memset(&hints, 0, sizeof hints);
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    if (getaddrinfo(host, NULL, &hints, &res) != 0 || !res) {
        snprintf(why, wn, "could not resolve the host name");
        return 0;
    }
    int first = 1;
    for (struct addrinfo *a = res; a; a = a->ai_next) {
        char ip[INET6_ADDRSTRLEN + 1] = "";
        if (a->ai_family == AF_INET) {
            const unsigned char *v = (const unsigned char *)&((struct sockaddr_in *)a->ai_addr)->sin_addr;
            int b[4] = { v[0], v[1], v[2], v[3] };
            if (aiweb_ipv4_blocked(b)) { snprintf(why, wn, "the host resolves to a private address"); freeaddrinfo(res); return 0; }
            inet_ntop(AF_INET, v, ip, sizeof ip);
        } else if (a->ai_family == AF_INET6) {
            const unsigned char *v = ((struct sockaddr_in6 *)a->ai_addr)->sin6_addr.s6_addr;
            int mapped = 1;
            for (int i = 0; i < 10; i++) if (v[i]) mapped = 0;
            if (v[10] != 0xff || v[11] != 0xff) mapped = 0;
            int b4[4] = { v[12], v[13], v[14], v[15] };
            int all0 = 1;
            for (int i = 0; i < 16; i++) if (v[i]) all0 = 0;
            /* only global unicast 2000::/3 is accepted; IPv4-mapped goes through the IPv4 rules */
            if (mapped ? aiweb_ipv4_blocked(b4) : (all0 || (v[0] & 0xE0) != 0x20)) {
                snprintf(why, wn, "the host resolves to a private address");
                freeaddrinfo(res);
                return 0;
            }
            inet_ntop(AF_INET6, v, ip, sizeof ip);
        } else continue;
        if (first) {
            first = 0;
            snprintf(pin, n, "%s:%d:%s", host, port, ip);
        }
    }
    freeaddrinfo(res);
    if (first) { snprintf(why, wn, "the host has no usable address"); return 0; }
    return 1;
}
#endif

char *aifetch_url(const char *start_url, int start) {
    char cur_url[2100];
    snprintf(cur_url, sizeof cur_url, "%s", start_url ? start_url : "");
    const char *url = cur_url;
    char *buf = NULL;
    char ctype_buf[200];
    const char *ctype = "";
    int hop;
    for (hop = 0; hop <= FETCH_MAX_HOPS; hop++) {
    char why[160];
    if (!aiweb_url_ok(url, why, sizeof why)) {
        char m[220];
        snprintf(m, sizeof m, "cannot fetch: %s", why);
        return err_str(m);
    }
    char pin[400] = "";
#ifndef _WIN32
    if (!resolve_public(url, pin, sizeof pin, why, sizeof why)) {
        char m[220];
        snprintf(m, sizeof m, "cannot fetch: %s", why);
        return err_str(m);
    }
#endif

    char cfg[2100 + 500];
    if (*pin) snprintf(cfg, sizeof cfg, "url = \"%s\"\nresolve = \"%s\"\n", url, pin);
    else snprintf(cfg, sizeof cfg, "url = \"%s\"\n", url);
    char cfg_path[512];
    if (!ai_write_temp(cfg_path, sizeof cfg_path, cfg)) return err_str("could not write a temp file");

    char cmd[1200];
    snprintf(cmd, sizeof cmd,
#ifdef _WIN32
             "curl.exe"
#else
             "curl"
#endif
             " -sS --max-time %d --proto =http,https"
             " -A \"velo-editor\" -K \"%s\" -w \"\\n%%{content_type}\\n%%{http_code}\\n%%{redirect_url}\" 2>&1",
             FETCH_TIMEOUT_S, cfg_path);

    buf = NULL;
    size_t len = 0;
    int too_big = 0;
    int status = ai_run_curl(cmd, &buf, &len, &too_big, FETCH_MAX_BYTES);
    remove(cfg_path);
    if (!buf) return err_str("could not run curl (is it installed?)");
    if (too_big) { free(buf); return err_str("the page is too large (over 2 MB)"); }
    if (status != 0) {
        const char *m = ai_curl_error(status);
        char tmp[200];
        if (!m) { snprintf(tmp, sizeof tmp, "request failed (curl exit %d)", status); m = tmp; }
        free(buf);
        return err_str(m);
    }

    /* The -w trailer is "\n<content type>\n<http code>\n<redirect url>" after the body. */
    char *nl3 = strrchr(buf, '\n');
    char *nl2 = nl3 ? (*nl3 = '\0', strrchr(buf, '\n')) : NULL;
    char *redirect = nl3 ? nl3 + 1 : (char *)"";
    int code = 0;
    ctype = "";
    if (nl2) {
        code = atoi(nl2 + 1);
        *nl2 = '\0';
        char *nl1 = strrchr(buf, '\n');
        if (nl1) {
            snprintf(ctype_buf, sizeof ctype_buf, "%s", nl1 + 1);
            ctype = ctype_buf;
            *nl1 = '\0';
        }
    }
    if (code >= 300 && code < 400 && *redirect) {
        if (hop == FETCH_MAX_HOPS || strlen(redirect) >= sizeof cur_url) {
            free(buf);
            return err_str("too many redirects");
        }
        snprintf(cur_url, sizeof cur_url, "%s", redirect);
        free(buf);
        buf = NULL;
        continue;   /* the next round checks the new URL the same way */
    }
    if (code < 200 || code >= 300) {
        char m[64];
        snprintf(m, sizeof m, "HTTP %d", code);
        free(buf);
        return err_str(m);
    }
    int is_html = ci_contains(ctype, "html");
    int texty = !*ctype || ci_contains(ctype, "text/") || ci_contains(ctype, "json") ||
                ci_contains(ctype, "xml") || ci_contains(ctype, "javascript") ||
                ci_contains(ctype, "yaml") || ci_contains(ctype, "markdown");
    if (!texty) {
        free(buf);
        return err_str("that URL is not a text page");
    }
    if (!is_html && !*ctype) {   /* no content type: sniff for markup */
        const char *s = buf;
        while (*s == ' ' || *s == '\n' || *s == '\r' || *s == '\t') s++;
        is_html = *s == '<';
    }
    char *text = is_html ? aiweb_html_to_text(buf, strlen(buf)) : buf;
    char *out = text ? aiweb_wrap_result(start_url, text, start) : NULL;
    if (text != buf) free(text);
    free(buf);
    return out ? out : err_str("out of memory");
    }
    return err_str("too many redirects");
}
