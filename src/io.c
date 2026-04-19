#include "io.h"

#include <errno.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
  #include <fcntl.h>
  #include <io.h>
#endif

static pthread_mutex_t write_mu = PTHREAD_MUTEX_INITIALIZER;

void io_init(void) {
#ifdef _WIN32
    /* Binary mode: keep CRLF translation out of the framing. */
    _setmode(_fileno(stdin),  _O_BINARY);
    _setmode(_fileno(stdout), _O_BINARY);
    _setmode(_fileno(stderr), _O_BINARY);
#endif
    setvbuf(stdout, NULL, _IONBF, 0);
    setvbuf(stderr, NULL, _IONBF, 0);
}

/* MCP stdio transport: newline-delimited JSON. Each message is a single
 * JSON-RPC object followed by '\n'. Messages MUST NOT contain embedded
 * newlines (the JSON emitter produces compact output, so this holds). */

int io_read_frame(char **buf, size_t *len) {
    char *body = NULL;
    size_t cap = 0, n = 0;
    int any = 0;

    for (;;) {
        int c = getc(stdin);
        if (c == EOF) {
            if (ferror(stdin) && errno == EINTR) { clearerr(stdin); continue; }
            if (!any) { free(body); return 1; }    /* clean eof */
            free(body);
            return -1;                              /* eof mid-message */
        }
        any = 1;
        if (c == '\n') {
            /* Strip a trailing \r if present (tolerant of CRLF writers). */
            if (n && body[n - 1] == '\r') n--;
            if (!body) { body = malloc(1); cap = 1; }
            body[n] = 0;
            *buf = body;
            *len = n;
            return 0;
        }
        if (n + 1 >= cap) {
            size_t nc = cap ? cap * 2 : 256;
            body = realloc(body, nc);
            cap = nc;
        }
        body[n++] = (char)c;
    }
}

int io_write_frame(const char *body, size_t len) {
    pthread_mutex_lock(&write_mu);
    int rc = 0;
    if (len && fwrite(body, 1, len, stdout) != len) rc = -1;
    if (!rc && fputc('\n', stdout) == EOF) rc = -1;
    fflush(stdout);
    pthread_mutex_unlock(&write_mu);
    return rc;
}
