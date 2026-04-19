/* stderr JSONL audit log. Thread-safe. */
#ifndef JMCP_LOG_H
#define JMCP_LOG_H

#include <stdarg.h>

/* printf-style free-form log line, prefixed with a timestamp. */
void jlog(const char *fmt, ...) __attribute__((format(printf, 1, 2)));

#endif
