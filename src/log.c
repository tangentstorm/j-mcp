#include "log.h"

#include <pthread.h>
#include <stdio.h>
#include <time.h>

static pthread_mutex_t log_mu = PTHREAD_MUTEX_INITIALIZER;

void jlog(const char *fmt, ...) {
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    struct tm tm;
    gmtime_r(&ts.tv_sec, &tm);
    char tbuf[96];
    snprintf(tbuf, sizeof tbuf, "%04d-%02d-%02dT%02d:%02d:%02d.%03ldZ",
             tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday,
             tm.tm_hour, tm.tm_min, tm.tm_sec, ts.tv_nsec / 1000000);

    pthread_mutex_lock(&log_mu);
    fprintf(stderr, "[%s] ", tbuf);
    va_list ap; va_start(ap, fmt); vfprintf(stderr, fmt, ap); va_end(ap);
    fputc('\n', stderr);
    pthread_mutex_unlock(&log_mu);
}
