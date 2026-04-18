#include "io.h"
#include "log.h"
#include "mcp.h"

#include <signal.h>
#include <stdio.h>

int main(int argc, char **argv) {
    (void)argc; (void)argv;

    /* SIGPIPE on write to a closed stdout should be ignored; we handle EPIPE. */
    signal(SIGPIPE, SIG_IGN);

    io_init();
    jlog("j-mcp starting");

    mcp_run();

    jlog("j-mcp exiting");
    return 0;
}
