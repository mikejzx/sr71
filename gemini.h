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
    // Current URI
    struct uri uri;

    // Buffer used for receiving data
    char *recv_buffer;
    size_t recv_buffer_size;

    SSL *ssl;
    SSL_CTX *ctx;
    int sock;
};

void gemini_init(void);
void gemini_deinit(void);

// Issue a request over Gemini
int gemini_request(struct uri *);

#endif
