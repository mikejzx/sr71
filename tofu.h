#ifndef TOFU_H
#define TOFU_H

#include "uri.h"

/*
 * tofu.h
 * Implements TOFU (trust-on-first-use) verification.  Works like OpenSSH, just
 * a single file with all known hosts, and their fingerprints.
 */

enum tofu_verify_status
{
    // Failed to verify because of an error
    TOFU_VERIFY_ERROR,

    // Fingerprints matched successfully
    TOFU_VERIFY_OK,

    // Fingerprints do not match
    TOFU_VERIFY_FAIL,

    // Host was unrecognised and hence cert fingerprint added to TOFU database
    // and trusted by default
    TOFU_VERIFY_NEW,
};

// Single entry in the TOFU database
struct tofu_entry
{
    char hostname[URI_HOSTNAME_MAX];
    unsigned char fingerprint[EVP_MAX_MD_SIZE];
    size_t fingerprint_len;
};

struct tofu
{
    struct tofu_entry *entries;
    size_t entry_count;
    size_t entry_capacity;
};

void tofu_init(void);
void tofu_deinit(void);
enum tofu_verify_status tofu_verify_or_add(char *, X509 *);

#endif
