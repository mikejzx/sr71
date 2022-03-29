#include "pch.h"
#include "cache.h"
#include "sighandle.h"
#include "state.h"
#include "status_line.h"
#include "tofu.h"
#include "tui.h"

struct state *g_state;
struct recv_buffer *g_recv;

void program_exited(void);

int
main(int argc, char **argv)
{
    g_state = calloc(1, sizeof(struct state));
    g_recv = &g_state->recv_buffer;

    // Create initial raw receive buffer
    g_recv->capacity = 4096;
    g_recv->b = malloc(g_recv->capacity);
    g_recv->size = 0;

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
#if 0
    static const char *PAGER_CONTENT =
        "This is a test\n"
        "Line 2\n"
        "Line 3\n"
        "Line 4\n"
        "Line 5\n"
        "# Main heading\n"
        "## Heading 2\n"
        "### Heading 3\n"
        "* List item 0 sajd ahsdl jasdk jasd jasd saj djsajd jasj jdajs dlas jdlasjld jajsl djas dljajd jasjd jajdjajda\n"
        "* List item 1\n"
        "* List item 2\n"
        "* List item 3\n"
        "\n"
        "=> gemini://gemini.circumlunar.space/ Gemini Homepage\n"
        "=> gemini://gemini.circumlunar.space/docs Gemini Documentation\n"
        "=> gemini://example.com/\n"
        "Empty line above here\n"
        "Test\n"
        "Line 5\n"
        "\n"
        "Empty line above here\n"
        "Test\n"
        "\n\n"
        "Lorem ipsum dolor sit amet, consectetur adipiscing dsahdjasdhjahdjsdhasjdasfdfsdfjsfdselifdsfsdfsasdasdasdftestingTESTINGtestingornarejsdlasdjsadlsadadsddiam"
        "vitaesdjaddasldas lobortis suscipit, dui lacus varius sem, eget varius nunc risus in purus."
        "Nunc lacinia tempus magna eget accumsan. Pellentesque dui nisi, euismod at"
        "porta quis, rutrum a nibh. Morbi vehicula sollicitudin molestie. Duis tempus"
        "lectus eu convallis ullamcorper. Nam elementum lorem nec est tempus bibendum."
        "Donec laoreet at felis quis semper."
        "Sed ac gravida libero. Nunc volutpat mauris quis elit fringilla, vitae rhoncus"
        "tortor sodales. Donec viverra nibh ac augue cursus, et tincidunt libero"
        "malesuada. Nulla pulvinar augue eu ipsum porta, vel fermentum metus convallis."
        "Sed accumsan blandit turpis eu ultrices. In porta hendrerit massa, sit amet"
        "porttitor quam feugiat id. Orci varius natoque penatibus et magnis dis"
        "parturient montes, nascetur ridiculus mus. Nullam lectus magna, consectetur ut"
        "sagittis eu, mollis ac est. Sed suscipit, nisl quis tincidunt scelerisque,"
        "nibh mi scelerisque ante, et interdum lacus nulla id metus. Curabitur sed"
        "vulputate nisi. Nulla facilisi. Etiam at egestas orci, a molestie ex. Nunc"
        "porta mauris at feugiat luctus. Sed eu justo fermentum, mollis tellus ac,"
        "rhoncus diam. Nunc gravida, nunc ut sagittis varius, elit quam tincidunt elit,"
        "vel venenatis lectus augue et metus. Integer egestas vitae odio non"
        "vestibulum."
        "\n\n"
        "NO SPACES:\n\n"
        " fdsf sdlfjk sdf lsd Donecegetvestibulumlibero.D''''''-==;'lis,pretiumeumattiseu,vehicula"
        "hendreritvelit.Sedvolutpategestastincidunt.Utamattisrisus.Integer"
        "egettempusnisi,sitametcondimentummetus.Inpharetramolestielorem,"
        "vehiculacondimentumesttinciduntid.Nullaetfelisvitaesapienmaximus"
        "congue.Etiamrisusurna,portaetpulvinareget,imperdietuttellus."
        "\n\n"
        "In nec velit tempus, posuere ipsum vitae, gravida ex. Nullam tristique eros id"
        "elit convallis, volutpat molestie enim fringilla. Morbi feugiat finibus nisi eu"
        "laoreet. Suspendisse ante orci, auctor sit amet cursus ut, imperdiet vitae"
        "urna. Ut eget mi vitae enim interdum maximus. Cras suscipit neque eget orci"
        "placerat, et rhoncus erat laoreet. Phasellus mauris libero, dapibus id urna"
        "mollis, maximus viverra metus. Phasellus finibus ligula vel orci accumsan, eget"
        "tincidunt augue tristique. Proin pulvinar ex risus, commodo congue leo"
        "imperdiet nec. Curabitur rhoncus rhoncus eros, at tempor eros vestibulum vitae."
        "Duis scelerisque nisi ac felis bibendum, maximus facilisis est malesuada."
        "Pellentesque vitae mi a eros placerat vestibulum. Aenean sagittis ipsum mi, ut"
        "porta ipsum mollis in. Phasellus ut cursus libero. Maecenas aliquam neque"
        "risus, ut congue sem aliquet sit amet. Quisque vulputate urna dui, et tristique"
        "ligula sagittis ultrices."
        "\n\n"
        "Aliquam dapibus metus nunc, non sodales sapien maximus nec. Nullam maximus"
        "consectetur ultricies. Praesent et nulla id mauris efficitur porttitor. Aenean"
        "vel laoreet velit, ac volutpat sem. Morbi vulputate, nisl eget vestibulum"
        "scelerisque, mauris orci mollis purus, efficitur laoreet risus dolor quis"
        "sapien. Aenean pellentesque \x1b[93;41mnisi eu dignissim testing \x1b[0mPellentesque dignissim"
        "mi ultricies diam aliquet, nec sodales ligula euismod. Duis eu pretium lorem."
        "Phasellus et massa vehicula, tristique augue at, imperdiet diam. Proin eget"
        "risus vitae lacus sollicitudin luctus"
        "\n"
        "UTF-8 test: ちゃぶ台返し (╯°□°)╯𐄻︵ ̲┻̲━̲┻̲ ̲o 𐌰𐌱𐌲𐌳𐌴𐌵𐌶𐌷\n"
        "test end of line";
#else
    static const char *PAGER_CONTENT =
        "# gemini client\n"
        "\n"
        "## Built with:\n"
        "* SSL: " OPENSSL_VERSION_TEXT "\n"
        "\n"
        "### Some links\n"
        "=> gemini://gemini.circumlunar.space/ Gemini Homepage\n"
        "=> gemini://gemini.circumlunar.space/docs/ Gemini Documentation\n"
        "=> gemini://example.com/\n"
        "=> gopher://i-logout.cz:70/1/bongusta Test gopher page\n"
        "=> gopher://gopher.quux.org:70/\n"
        "=> gopher://gopher.floodgap.com Floodgap\n"
        "=> file:///home/mike/pages/gemtext/gemini.circumlunar.space/home.gmi Local file test\n"
        "=> file:///home/mike/ Local directory test\n"
        "=> gemini://example.com/ A link with a very long name that will wrap around and hopefully work properly\n"
        "=> gemini://example.com/\n"
        "=> gemini://midnight.pub/\n"
        "=> gemini://rawtext.club/~ploum/2022-03-24-ansi_html.gmi/\n"
        "=> gemini://zaibatsu.circumlunar.space/~solderpunk Zaibatsu - solderpunk\n"
        "\n"
        "# Very long heading that should wrap very nicely blah blah blah blah blah\n"
        "This is a test paragraph\n"
        "> This is a test blockquote that should also wrap pretty nice I reckon, blah blah blah\n"
        "> This is a test blockquote that should also wrap pretty nice I reckon, blah blah blah\n"
        "This is a test paragraph\n";
#endif
    if (argmode == CMD_ARGS_NONE)
    {
        g_recv->size = strlen(PAGER_CONTENT) + 1;
        recv_buffer_check_size(g_recv->size);
        memcpy(g_recv->b, PAGER_CONTENT, g_recv->size);
        mime_parse(&g_recv->mime, MIME_GEMTEXT, strlen(MIME_GEMTEXT));

        // Typeset the content
        pager_update_page(-1, 0);
    }

    for(;!g_sigint_caught;)
    {
        if (tui_update() < 0) break;
    }

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
}
