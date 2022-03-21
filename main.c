#include "pch.h"
#include "sighandle.h"
#include "state.h"
#include "status_line.h"
#include "tui.h"

struct state *g_state;

void program_exited(void);

int
main(void)
{
    g_state = calloc(1, sizeof(struct state));

    sighandle_register();

    pager_init();
    status_line_init();
    tui_init();

    gemini_init();

    // Set some temporary content
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
        "sapien. Aenean pellentesque \x1b[93;41mnisi eu dignissim\x1b[0m dignissim. Pellentesque dignissim"
        "mi ultricies diam aliquet, nec sodales ligula euismod. Duis eu pretium lorem."
        "Phasellus et massa vehicula, tristique augue at, imperdiet diam. Proin eget"
        "risus vitae lacus sollicitudin luctus"
        "\n"
        "UTF-8 test: ちゃぶ台返し (╯°□°)╯𐄻︵ ̲┻̲━̲┻̲ ̲o 𐌰𐌱𐌲𐌳𐌴𐌵𐌶𐌷\n"
        "test end of line";

    // Typeset the content
    pager_update_page(PAGER_CONTENT, strlen(PAGER_CONTENT) + 1);

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
    gemini_deinit();

    tui_cleanup();

    free(g_state);
}
