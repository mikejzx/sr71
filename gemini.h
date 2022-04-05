#ifndef GEMINI_H
#define GEMINI_H

#include "uri.h"

/*
 * gemini.h
 *
 * Gemini client code
 */

#define GEMINI_MAX_CONSECUTIVE_REDIRECTS 5

struct gemini
{
    SSL *ssl;
    SSL_CTX *ctx;
    int sock;

    // Consecutive redirect count
    int redirects;

    // Last attempted URI to access
    struct uri last_uri_attempted;
};

void gemini_init(void);
void gemini_deinit(void);

// Issue a request over Gemini
int gemini_request(struct uri *);

#endif
