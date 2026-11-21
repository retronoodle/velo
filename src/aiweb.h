/* Pure helpers for the chat's fetch_url tool: URL checks, the allow-list test, HTML to text
   and result wrapping. No I/O (the request itself is in aifetch.c). */
#ifndef AIWEB_H
#define AIWEB_H

#include <stddef.h>

#define AIWEB_MAX_OUT 30000   /* bytes of page text returned by one call */

/* 1 if url is fetchable: http/https, plain URL characters only, no user:pass@, a host name with
   a dot (no localhost, .local, .internal, IPv6 literals, odd number forms) and not a private,
   loopback or link-local IPv4 address. Otherwise 0 and err (size n) says why. */
int aiweb_url_ok(const char *url, char *err, size_t n);

/* 1 if the dotted quad b[0..3] is private, loopback, link-local, reserved or otherwise not
   routable on the public internet. */
int aiweb_ipv4_blocked(const int b[4]);

/* Host name and port (default 80/443) of an http/https URL. 0 if it can't be parsed. */
int aiweb_url_host(const char *url, char *host, size_t n, int *port);

/* 1 if url appears in text as a whole URL (not as the start or the end of a longer one). A trailing
   . , ; : ! after it in the text is allowed. */
int aiweb_url_in_text(const char *text, const char *url);

/* HTML to readable text (malloc'd): drops script/style/comments/tags, decodes common entities,
   keeps line breaks at block tags and collapses other whitespace. */
char *aiweb_html_to_text(const char *html, size_t len);

/* The tool result: a header naming url (marked as untrusted page text), the slice of text from
   byte offset start (cut to AIWEB_MAX_OUT on a character boundary), and a note with the next
   start value when there is more. malloc'd. */
char *aiweb_wrap_result(const char *url, const char *text, int start);

#endif
