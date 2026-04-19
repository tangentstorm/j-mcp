/* LSP-style Content-Length framing on stdin/stdout. */
#ifndef JMCP_IO_H
#define JMCP_IO_H

#include <stddef.h>

/* Read one frame from stdin. On success: *buf = malloc'd body (NUL-terminated),
 * *len = body length (excluding NUL), returns 0. On EOF: returns 1. On error:
 * returns -1. Caller frees *buf. */
int io_read_frame(char **buf, size_t *len);

/* Write one frame to stdout. Returns 0 on success, -1 on error.
 * Thread-safe: serialises writes via an internal mutex. */
int io_write_frame(const char *body, size_t len);

/* Disable stdout buffering. Call once at startup. */
void io_init(void);

#endif
