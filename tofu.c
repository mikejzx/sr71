#include "pch.h"
#include "tofu.h"
#include "tui.h"

void
tofu_init(void)
{
    FILE *db = fopen(TOFU_FILE_PATH, "r");
    if (!db)
    {
        tui_cmd_status_prepare();
        tui_say("error: failed to open TOFU database '" TOFU_FILE_PATH "'");
        return;
    }

    fclose(db);
}

void
tofu_deinit(void)
{
    // Write any modifications to the TOFU database file
    FILE *db = fopen(TOFU_FILE_PATH, "r");
    if (!db)
    {
        tui_cmd_status_prepare();
        tui_say("error: failed to open TOFU database '" TOFU_FILE_PATH "'");
        return;
    }

    fclose(db);
}
