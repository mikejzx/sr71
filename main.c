#include "pch.h"
#include "cache.h"
#include "sighandle.h"
#include "state.h"
#include "status_line.h"
#include "tofu.h"
#include "tui.h"

#include "line_break_alg.h"
#include "hyphenate_alg.h"

struct state *g_state;
struct recv_buffer *g_recv;

void program_exited(void);

int
main(int argc, char **argv)
{
    setlocale(LC_ALL, "");

    utf8_init();

#if 0
    const char *word = "hyphenate";
    hyphenate(word, strlen(word));

    printf("%s: ", word);
    int last = 0;
    for (int h = hyphenate_get(); h != -1; h = hyphenate_get())
    {
        printf("%.*s-", h - last, word + last);
        last = h;
    }
    printf("%.*s", (int)(strlen(word) - last), word + last);
    printf("\n");

    return 0;
#endif

    g_state = calloc(1, sizeof(struct state));
    g_recv = &g_state->recv_buffer;

    // Create initial raw receive buffer
    g_recv->capacity = 4096;
    g_recv->b = malloc(g_recv->capacity);
    g_recv->size = 0;
    g_recv->b_alt = NULL;

    history_init();

    sighandle_register();

    pager_init();
    status_line_init();
    tui_init();

    gemini_init();
    tofu_init();
    cache_init();

    enum cmd_arg_mode
    {
        CMD_ARGS_NONE = 0,
        CMD_ARGS_FILE,
        CMD_ARGS_URI,
    } argmode = CMD_ARGS_NONE;

    // Check the command-line args for content
    for (++argv; *argv != NULL; ++argv)
    {
        if (access(*argv, F_OK) == 0)
        {
            argmode = CMD_ARGS_FILE;

            // Load the given file into the pager
            struct uri uri;
            memset(&uri, 0, sizeof(struct uri));
            uri.protocol = PROTOCOL_FILE;
            strncpy(uri.path, *argv, URI_PATH_MAX);

            tui_go_to_uri(&uri, true, false);

            break;
        }

        // See if we can parse a URI
        struct uri uri = uri_parse(*argv, strlen(*argv));
        if (tui_go_to_uri(&uri, true, false) == 0)
        {
            argmode = CMD_ARGS_URI;
            break;
        }
    }

    // Set some temporary content
    static const char *PAGER_CONTENT =
        "# sr71\n"
        "\n"
        "## Built with:\n"
        "* SSL: " OPENSSL_VERSION_TEXT "\n"
#if TYPESET_LINEBREAK_GREEDY
        "* Line breaking algorithm: Greedy\n"
#else
        "* Line breaking algorithm: Knuth-Plass\n"
#endif
        "\n"
        "### Some links\n"
        "=> gemini://gemini.circumlunar.space/ Gemini Homepage\n"
        "=> gemini://gemini.circumlunar.space/docs/ Gemini Documentation\n"
        "=> gemini://example.com/\n"
        "=> gopher://i-logout.cz:70/1/bongusta Test gopher page\n"
        "=> gopher://gopher.quux.org:70/\n"
        "=> gopher://gopher.quix.us:70/\n"
        "=> gopher://gopher.floodgap.com Floodgap\n"
        "=> gopher://1436.ninja/0/Phlog/20190831.post This gopherhole is brokn\n"
        "=> file:///home/mike/pages/gemtext/gemini.circumlunar.space/home.gmi Local file test\n"
        "=> file:///home/mike/ Local directory test\n"
        "=> gemini://example.com/ A link with a very long name that will wrap around and hopefully work properly\n"
        "=> gemini://example.com/\n"
        "=> gemini://midnight.pub/\n"
        "=> gemini://rawtext.club/~ploum/2022-03-24-ansi_html.gmi/\n"
        "=> gemini://zaibatsu.circumlunar.space/~solderpunk Zaibatsu - solderpunk\n"
        "=> gemini://1436.ninja/ broken rendering\n"
        "\n"
        "# Very long heading that should wrap very nicely blah blah blah blah blah\n"
        "This is a test paragraph\n"
        "> This is a test blockquote that should also wrap pretty nice I reckon, blah blah blah\n"
        "> This is a test blockquote that should also wrap pretty nice I reckon, blah blah blah\n"
        "This is a test paragraph\n"
        "Stupidly long word here that hopefullywillgetdetectedbythesearchingalgorithmthingtestintetsfdidsifisdfidsifsdiihyphenationsrtiewroewirewoirweiriweiriweri\n"
        "This is a test paragraph\n";
    if (argmode == CMD_ARGS_NONE)
    {
        g_recv->size = strlen(PAGER_CONTENT) + 1;
        recv_buffer_check_size(g_recv->size);
        memcpy(g_recv->b, PAGER_CONTENT, g_recv->size);
        mime_parse(&g_recv->mime, MIME_GEMTEXT, strlen(MIME_GEMTEXT));
        g_state->uri = uri_parse(URI_INTERNAL_BLANK, strlen(URI_INTERNAL_BLANK));

        // Typeset the content
        pager_update_page(-1, 0);
    }

    for(;!g_sigint_caught && tui_update() == 0;);

    program_exited();

    return 0;
}

void
program_exited(void)
{
    // Only exit once
    static bool exited = false;
    if (exited) return;
    exited = true;

    cache_deinit();
    tofu_deinit();

    gemini_deinit();
    gopher_deinit();

    tui_cleanup();

    history_deinit();

    free(g_recv->b);
    free(g_state);

    utf8_deinit();
}
