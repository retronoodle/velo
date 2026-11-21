/* The network side of the chat's fetch_url tool: one blocking GET through curl. */
#ifndef AIFETCH_H
#define AIFETCH_H

/* Fetches url (checked with aiweb_url_ok) and returns the page as text for the model,
   starting at byte offset start of the converted text. Always malloc'd; failures come back
   as "Error: ..." text. The caller decides whether the URL is allowed at all. */
char *aifetch_url(const char *url, int start);

#endif
