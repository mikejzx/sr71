#include "pch.h"
#include "tofu.h"
#include "tui.h"

static struct tofu s_tofu;

static inline bool
tofu_file_exists(void) { return access(TOFU_FILE_PATH, F_OK) == 0; }

void
tofu_init(void)
{
    s_tofu.entry_count = 0;
    s_tofu.entry_capacity = 128;
    s_tofu.entries = malloc(sizeof(struct tofu_entry) * s_tofu.entry_capacity);

    if (!tofu_file_exists()) { return; }

    // Read saved entries
    FILE *fp = fopen(TOFU_FILE_PATH, "r");
    if (!fp)
    {
        tui_status_begin();
        tui_say("error: failed to open TOFU database '" TOFU_FILE_PATH "'");
        tui_status_end();
        return;
    }

    size_t len_tmp;
    ssize_t len;
    char *line;
    for (line = NULL; (len = getline(&line, &len_tmp, fp)) != -1;)
    {
        ssize_t delim;
        for (delim = 0; delim < len && line[delim] != ' '; ++delim);
        if (delim >= len) continue;
        if (line[len - 1] == '\n')
        {
            line[len - 1] = '\0';
            --len;
        }

        struct tofu_entry *entry = &s_tofu.entries[s_tofu.entry_count++];
        memset(entry, 0, sizeof(struct tofu_entry));
        strncpy(entry->hostname, line, min(delim, URI_HOSTNAME_MAX));

        entry->fingerprint_len = 0;
        for (const char *x = line + delim + 1;
            x < line + len;
            x += 3)
        {
            entry->fingerprint[entry->fingerprint_len] =
                strtol(x, NULL, 16);
            ++entry->fingerprint_len;
        }
    }
    if (line) free(line);

    fclose(fp);
}

void
tofu_deinit(void)
{
    // Update the TOFU database on disk
    FILE *fp = fopen(TOFU_FILE_PATH, "w");
    if (!fp)
    {
        tui_status_begin();
        tui_say("error: failed to open TOFU database '" TOFU_FILE_PATH "'");
        tui_status_end();
        free(s_tofu.entries);
        return;
    }

    for (int i = 0; i < s_tofu.entry_count; ++i)
    {
        struct tofu_entry *entry = &s_tofu.entries[i];
        fprintf(fp, "%s ", entry->hostname);

        // Write fingerprint as a string.  If we use raw bytes then some
        // fingerprints screw up the getline if they contain the '\n' character
        for (int b = 0; b < entry->fingerprint_len; ++b)
        {
            fprintf(fp, "%02x%s",
                entry->fingerprint[b],
                b == entry->fingerprint_len - 1 ? "" : ":");
        }
        fprintf(fp, "\n");
    }

    fclose(fp);
    free(s_tofu.entries);
}

/* Verify host certificate fingerprint */
enum tofu_verify_status
tofu_verify_or_add(char *hostname, X509 *cert)
{
    // Generate fingerprint from the given certificate
    const EVP_MD *digest = EVP_get_digestbyname("sha256");
    unsigned char fingerprint[EVP_MAX_MD_SIZE];
    unsigned fingerprint_len;
    if (X509_digest(cert, digest, fingerprint, &fingerprint_len) != 1)
    {
        return TOFU_VERIFY_ERROR;
    }

    // Iterate over certificates in our database
    for (int i = 0; i < s_tofu.entry_count; ++i)
    {
        const struct tofu_entry *entry = &s_tofu.entries[i];

        if (strncmp(entry->hostname, hostname, URI_HOSTNAME_MAX) == 0)
        {
            // We have this host in our database; now compare fingerprints
            return memcmp(entry->fingerprint, fingerprint, fingerprint_len) == 0
                ? TOFU_VERIFY_OK
                : TOFU_VERIFY_FAIL;
        }
    }

    /* Add fingerprint to TOFU database */
    if (s_tofu.entry_capacity < s_tofu.entry_count + 1)
    {
        // Reallocate if needed
        s_tofu.entry_capacity *= 3;
        s_tofu.entry_capacity /= 2;
        void *tmp = realloc(s_tofu.entries,
            s_tofu.entry_capacity * sizeof(struct tofu_entry));
        if (!tmp)
        {
            fprintf(stderr, "out of memory\n");
            exit(-1);
        }
        s_tofu.entries = tmp;
    }

    struct tofu_entry *ent = &s_tofu.entries[s_tofu.entry_count++];
    *ent->hostname = '\0';
    strncpy(ent->hostname, hostname, URI_HOSTNAME_MAX);
    *ent->fingerprint = '\0';
    memcpy(ent->fingerprint, fingerprint, fingerprint_len);
    ent->fingerprint_len = fingerprint_len;

    return TOFU_VERIFY_NEW;
}
