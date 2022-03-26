#ifndef TOFU_H
#define TOFU_H

#define TOFU_FILE_PATH "tofu"

/*
 * tofu.h
 * Implements TOFU (trust-on-first-use) verification.  Works like OpenSSH, just
 * a single file with all known hosts, and their fingerprints.
 */

// Single entry in the TOFU database
struct tofu_entry
{
    char hostname[256];
    char fingerprint[EVP_MAX_MD_SIZE];
};

void tofu_init(void);
void tofu_deinit(void);

#endif
