#ifndef GEMINI_H
#define GEMINI_H

#include "uri.h"

/*
 * gemini.h
 *
 * Gemini client code
 */

struct gemini
{
    SSL *ssl;
    SSL_CTX *ctx;
    int sock;
};

void gemini_init(void);
void gemini_deinit(void);

// Issue a request over Gemini
int gemini_request(struct uri *);

#endif
