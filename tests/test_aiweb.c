/* Unit tests for aiweb.c: URL checks, allow-list matching, HTML to text, result wrapping. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "aiweb.h"

static int failures = 0;
#define CHECK(c, msg) do { if (!(c)) { printf("FAIL %s\n", msg); failures++; } } while (0)

static void ok(const char *u, int want) {
    char e[100];
    if (aiweb_url_ok(u, e, sizeof e) != want) { printf("FAIL url_ok(\"%s\") want %d (%s)\n", u, want, e); failures++; }
}

static void html_is(const char *in, const char *want) {
    char *t = aiweb_html_to_text(in, strlen(in));
    if (!t || strcmp(t, want) != 0) { printf("FAIL html(\"%s\") = \"%s\", want \"%s\"\n", in, t ? t : "(null)", want); failures++; }
    free(t);
}

int main(void) {
    ok("https://example.com/docs?a=1#x", 1);
    ok("HTTP://Example.com", 1);
    ok("http://example.com:8080/x", 1);
    ok("http://93.184.216.34/x", 1);
    ok("ftp://example.com/x", 0);
    ok("file:///etc/passwd", 0);
    ok("example.com", 0);
    ok("", 0);
    ok("http://localhost/x", 0);
    ok("http://intranet/x", 0);
    ok("http://foo.local/x", 0);
    ok("http://x.localhost/x", 0);
    ok("http://127.0.0.1/x", 0);
    ok("http://10.1.2.3/", 0);
    ok("http://172.16.0.1/", 0);
    ok("http://172.32.0.1/", 1);
    ok("http://192.168.1.1/", 0);
    ok("http://169.254.169.254/latest", 0);
    ok("http://0.0.0.0/", 0);
    ok("http://2130706433/", 0);
    ok("http://0x7f.0.0.1/", 0);
    ok("http://0177.0.0.1/", 0);
    ok("http://[::1]/", 0);
    ok("http://good.com@127.0.0.1/", 0);
    ok("http://good.com@evil.com/", 0);
    ok("https://example.com/a b", 0);
    ok("https://example.com/\"x", 0);
    ok("https://example.com/$(id)", 0);
    ok("https://example.com/`id`", 0);
    ok("https://example.com:80a/", 0);

    CHECK(aiweb_url_in_text("see https://a.com/x for more", "https://a.com/x"), "in text");
    CHECK(aiweb_url_in_text("see https://a.com/x.", "https://a.com/x"), "trailing period");
    CHECK(aiweb_url_in_text("(https://a.com/x)", "https://a.com/x"), "in parens");
    CHECK(aiweb_url_in_text("https://a.com/x", "https://a.com/x"), "whole text");
    CHECK(!aiweb_url_in_text("see https://a.com/xyz", "https://a.com/x"), "prefix of a longer URL");
    CHECK(!aiweb_url_in_text("see https://a.com/x.html", "https://a.com/x"), "prefix before .html");
    CHECK(!aiweb_url_in_text("see https://a.com/x?q=1", "https://a.com/x"), "prefix before query");
    CHECK(aiweb_url_in_text("a https://a.com/xy b https://a.com/x c", "https://a.com/x"), "second match");
    CHECK(!aiweb_url_in_text("nothing here", "https://a.com/x"), "absent");
    CHECK(!aiweb_url_in_text(NULL, "https://a.com/x"), "null text");

    html_is("<p>Hello <b>big</b>   world</p>", "Hello big world");
    html_is("<h1>Title</h1><p>One</p><p>Two</p>", "Title\n\nOne\n\nTwo");
    html_is("a<br>b", "a\nb");
    html_is("<ul><li>x</li><li>y</li></ul>", "- x\n- y");
    html_is("x<script>var a = '<b>';</script>y", "xy");
    html_is("<style>p{}</style>ok", "ok");
    html_is("<head><title>T</title></head><body>B</body>", "T\n\nB");
    html_is("a<!-- hidden -->b", "ab");
    html_is("1 &lt; 2 &amp;&amp; 3 &gt; 2 &quot;q&quot; &#39;s&#39; &#x41;", "1 < 2 && 3 > 2 \"q\" 's' A");
    html_is("a&nbsp;b", "a b");
    html_is("AT&T &bogus; x", "AT&T &bogus; x");
    html_is("&#233;", "\xC3\xA9");
    html_is("a < b", "a < b");
    html_is("<a href=\"x>y\">link</a>", "link");
    html_is("text<div", "text");
    html_is("<script>never closed", "");
    html_is("<!DOCTYPE html><html><body>hi</body></html>", "hi");
    html_is("<table><tr><td>a</td><td>b</td></tr></table>", "a b");
    html_is("", "");
    html_is("<svg class=\"i\"/><p>after</p>", "after");
    html_is("<html><title>T</title><p>no head close</p>", "T\n\nno head close");
    html_is("a<svg><path/></svg>b<svg/>c", "abc");
    html_is("<noscript>unclosed<p>kept</p>", "unclosed\n\nkept");
    html_is("<pre>a\n  b</pre>x", "a\n  b\n\nx");
    {
        int pt = 0; char h[100];
        CHECK(aiweb_url_host("https://a.com:8443/x", h, sizeof h, &pt) && !strcmp(h, "a.com") && pt == 8443, "host:port");
        CHECK(aiweb_url_host("http://a.com/x", h, sizeof h, &pt) && !strcmp(h, "a.com") && pt == 80, "host default port");
        CHECK(aiweb_url_host("https://a.com", h, sizeof h, &pt) && pt == 443, "https default port");
        int q[4] = { 127, 0, 0, 1 }, r2[4] = { 93, 184, 216, 34 };
        CHECK(aiweb_ipv4_blocked(q) && !aiweb_ipv4_blocked(r2), "ipv4 blocked");
    }
    CHECK(!aiweb_url_in_text("http://a.com/?u=https://evil.com/p", "https://evil.com/p"), "glued inside another URL");
    CHECK(!aiweb_url_in_text("xhttps://a.com/x", "https://a.com/x"), "glued to a word");
    CHECK(aiweb_url_in_text("\"https://a.com/x\"", "https://a.com/x"), "in quotes");

    char *r = aiweb_wrap_result("https://a.com/", "hello", 0);
    CHECK(r && strstr(r, "untrusted") && strstr(r, "https://a.com/") && strstr(r, "hello") && !strstr(r, "truncated"), "wrap short");
    free(r);
    char *big = malloc(AIWEB_MAX_OUT * 2 + 1);
    memset(big, 'a', AIWEB_MAX_OUT * 2);
    big[AIWEB_MAX_OUT * 2] = '\0';
    r = aiweb_wrap_result("https://a.com/", big, 0);
    char want[64];
    snprintf(want, sizeof want, "start=%d]", AIWEB_MAX_OUT);
    CHECK(r && strstr(r, "truncated") && strstr(r, want), "wrap truncated gives next start");
    free(r);
    r = aiweb_wrap_result("https://a.com/", big, AIWEB_MAX_OUT);
    CHECK(r && !strstr(r, "truncated"), "wrap second page has no note");
    free(r);
    r = aiweb_wrap_result("https://a.com/", big, 999999999);
    CHECK(r && strstr(r, "past the end"), "start past the end");
    free(r);
    /* a cut must not split a UTF-8 character */
    memset(big, 'a', AIWEB_MAX_OUT - 1);
    big[AIWEB_MAX_OUT - 1] = '\xC3'; big[AIWEB_MAX_OUT] = '\xA9'; big[AIWEB_MAX_OUT + 1] = '\0';
    r = aiweb_wrap_result("https://a.com/", big, 0);
    snprintf(want, sizeof want, "start=%d]", AIWEB_MAX_OUT - 1);
    CHECK(r && strstr(r, want), "utf-8 boundary");
    free(r);
    free(big);

    if (failures == 0) printf("aiweb: all tests passed\n");
    return failures ? 1 : 0;
}
