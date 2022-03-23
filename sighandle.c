#include "pch.h"
#include "sighandle.h"
#include "tui.h"

volatile bool g_sigint_caught = false;

/* Interrupt/termination handler */
static void
handle_sigint_sigterm(int param)
{
    (void)param;
    exit(0);
}

/* Window resize handler */
static void
handle_sigwinch(int param)
{
    (void)param;

    // Update TUI dimensions
    tui_resized();
}

/* Register signal handlers */
void
sighandle_register(void)
{
    // Register signal termination and interrupt handlers
    struct sigaction sigact;
    memset(&sigact, 0, sizeof(sigact));
    sigact.sa_handler = handle_sigint_sigterm;
    sigaction(SIGINT, &sigact, NULL);
    sigaction(SIGTERM, &sigact, NULL);

    // Register window resize signal handler
    sigact.sa_handler = handle_sigwinch;
    sigaction(SIGWINCH, &sigact, NULL);
}
