#include "io.h"

#include <errno.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
  #include <fcntl.h>
  #include <io.h>
  #define strncasecmp _strnicmp
#else
  #include <strings.h>
  #include <sys/uio.h>
  #include <unistd.h>
#endif

static pthread_mutex_t write_mu = PTHREAD_MUTEX_INITIALIZER;

void io_init(void) {
#ifdef _WIN32
    /* Keep CRLF out of the JSON-RPC framing: force binary mode on stdio. */
    _setmode(_fileno(stdin),  _O_BINARY);
    _setmode(_fileno(stdout), _O_BINARY);
    _setmode(_fileno(stderr), _O_BINARY);
#endif
    setvbuf(stdout, NULL, _IONBF, 0);
    setvbuf(stderr, NULL, _IONBF, 0);
}

/* Read exactly n bytes using fread (portable across Windows + POSIX). */
static int read_exact(FILE *f, char *buf, size_t n) {
    size_t got = 0;
    while (got < n) {
        size_t r = fread(buf + got, 1, n - got, f);
        if (r == 0) {
            if (feof(f))  return got == 0 ? 1 : -1;
            if (ferror(f)) {
                if (errno == EINTR) { clearerr(f); continue; }
                return -1;
            }
        }
        got += r;
    }
    return 0;
}

static int read_byte(FILE *f, char *c) {
    for (;;) {
        size_t r = fread(c, 1, 1, f);
        if (r == 1) return 1;
        if (feof(f)) return 0;
        if (ferror(f)) {
            if (errno == EINTR) { clearerr(f); continue; }
            return -1;
        }
    }
}

/* Read an LSP header block, returning content length. Returns -1 on eof, -2 on err. */
static long read_headers(FILE *f) {
    long content_length = -1;
    char line[256];
    size_t li = 0;
    int last_was_cr = 0;
    int headers_done = 0;
    int any_byte = 0;

    while (!headers_done) {
        char c;
        int r = read_byte(f, &c);
        if (r == 0) return any_byte ? -2 : -1;
        if (r < 0)  return -2;
        any_byte = 1;

        if (c == '\n') {
            if (li == 0 && last_was_cr) {
                /* blank line after a CRLF — end of headers */
                headers_done = 1;
                break;
            }
            line[li] = 0;
            /* strip trailing \r */
            if (li && line[li - 1] == '\r') line[--li] = 0;
            if (li == 0) { headers_done = 1; break; }
            /* parse header */
            if (!strncasecmp(line, "Content-Length:", 15)) {
                char *p = line + 15;
                while (*p == ' ' || *p == '\t') p++;
                content_length = strtol(p, NULL, 10);
            }
            li = 0;
            last_was_cr = 0;
        } else {
            if (li + 1 < sizeof line) line[li++] = c;
            last_was_cr = (c == '\r');
        }
    }
    return content_length;
}

int io_read_frame(char **buf, size_t *len) {
    long n = read_headers(stdin);
    if (n == -1) return 1;   /* clean eof */
    if (n < 0)   return -1;
    if (n == 0) {
        *buf = calloc(1, 1);
        *len = 0;
        return 0;
    }
    char *body = malloc((size_t)n + 1);
    if (!body) return -1;
    int r = read_exact(stdin, body, (size_t)n);
    if (r != 0) { free(body); return r > 0 ? 1 : -1; }
    body[n] = 0;
    *buf = body;
    *len = (size_t)n;
    return 0;
}

static int fwrite_all(const char *p, size_t n, FILE *f) {
    while (n) {
        size_t w = fwrite(p, 1, n, f);
        if (w == 0) {
            if (ferror(f)) {
                if (errno == EINTR) { clearerr(f); continue; }
                return -1;
            }
            return -1;
        }
        p += w; n -= w;
    }
    return 0;
}

int io_write_frame(const char *body, size_t len) {
    char header[64];
    int hlen = snprintf(header, sizeof header, "Content-Length: %zu\r\n\r\n", len);
    if (hlen < 0) return -1;

    pthread_mutex_lock(&write_mu);
    int rc = 0;
    if (fwrite_all(header, (size_t)hlen, stdout) < 0) rc = -1;
    else if (fwrite_all(body, len, stdout) < 0)       rc = -1;
    fflush(stdout);
    pthread_mutex_unlock(&write_mu);
    return rc;
}
