#include "io.h"

#include <strings.h>
#include <errno.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/uio.h>
#include <unistd.h>

static pthread_mutex_t write_mu = PTHREAD_MUTEX_INITIALIZER;

void io_init(void) {
    setvbuf(stdout, NULL, _IONBF, 0);
    setvbuf(stderr, NULL, _IONBF, 0);
}

/* Read exactly n bytes, retrying on EINTR. Returns 0 ok, 1 eof, -1 error. */
static int read_exact(int fd, char *buf, size_t n) {
    size_t got = 0;
    while (got < n) {
        ssize_t r = read(fd, buf + got, n - got);
        if (r < 0) {
            if (errno == EINTR) continue;
            return -1;
        }
        if (r == 0) return got == 0 ? 1 : -1;
        got += (size_t)r;
    }
    return 0;
}

/* Read one byte. Returns 1 ok, 0 eof, -1 error. */
static int read_byte(int fd, char *c) {
    for (;;) {
        ssize_t r = read(fd, c, 1);
        if (r == 1) return 1;
        if (r == 0) return 0;
        if (errno == EINTR) continue;
        return -1;
    }
}

/* Read an LSP header block, returning content length. Returns -1 on eof, -2 on err. */
static long read_headers(int fd) {
    long content_length = -1;
    char line[256];
    size_t li = 0;
    int last_was_cr = 0;
    int headers_done = 0;
    int any_byte = 0;

    while (!headers_done) {
        char c;
        int r = read_byte(fd, &c);
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
    long n = read_headers(0);
    if (n == -1) return 1;   /* clean eof */
    if (n < 0)   return -1;
    if (n == 0) {
        *buf = calloc(1, 1);
        *len = 0;
        return 0;
    }
    char *body = malloc((size_t)n + 1);
    if (!body) return -1;
    int r = read_exact(0, body, (size_t)n);
    if (r != 0) { free(body); return r > 0 ? 1 : -1; }
    body[n] = 0;
    *buf = body;
    *len = (size_t)n;
    return 0;
}

int io_write_frame(const char *body, size_t len) {
    char header[64];
    int hlen = snprintf(header, sizeof header, "Content-Length: %zu\r\n\r\n", len);
    if (hlen < 0) return -1;

    pthread_mutex_lock(&write_mu);
    struct iovec iov[2] = {
        { header,       (size_t)hlen },
        { (void *)body, len          },
    };
    size_t total = (size_t)hlen + len;
    size_t done = 0;
    int rc = 0;
    while (done < total) {
        /* adjust iov for partial writes */
        struct iovec v[2]; int nv = 0;
        size_t off = done;
        for (int i = 0; i < 2; i++) {
            if (off >= iov[i].iov_len) { off -= iov[i].iov_len; continue; }
            v[nv].iov_base = (char *)iov[i].iov_base + off;
            v[nv].iov_len  = iov[i].iov_len - off;
            nv++;
            off = 0;
            for (int j = i + 1; j < 2; j++) v[nv++] = iov[j];
            break;
        }
        ssize_t w = writev(1, v, nv);
        if (w < 0) {
            if (errno == EINTR) continue;
            rc = -1; break;
        }
        done += (size_t)w;
    }
    pthread_mutex_unlock(&write_mu);
    return rc;
}
