#include "io.h"
#include "log.h"
#include "mcp.h"
#include "session.h"
#include "tools_j.h"

#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void usage(const char *prog) {
    fprintf(stderr,
        "usage: %s [--libj <path>]\n"
        "  --libj <path>   path to libj.so/.dylib/.dll (default: libj.so via linker)\n",
        prog);
}

int main(int argc, char **argv) {
    const char *libj_path = NULL;

    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--libj") && i + 1 < argc) { libj_path = argv[++i]; }
        else if (!strcmp(argv[i], "-h") || !strcmp(argv[i], "--help")) { usage(argv[0]); return 0; }
        else { usage(argv[0]); return 2; }
    }

    signal(SIGPIPE, SIG_IGN);
    io_init();
    jlog("j-mcp starting");

    const char *err = NULL;
    if (session_system_init(libj_path, &err) < 0) {
        jlog("fatal: %s", err ? err : "libj load failed");
        return 1;
    }

    tools_j_register();

    mcp_run();

    jlog("j-mcp exiting");
    return 0;
}
