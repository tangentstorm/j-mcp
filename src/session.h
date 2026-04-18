/* Named J sessions. Each session owns a pthread worker and a libj instance.
 * All JDo calls happen on the worker; the main thread only posts work. */
#ifndef JMCP_SESSION_H
#define JMCP_SESSION_H

#include "json.h"

#include <stddef.h>

typedef struct session session;

typedef struct {
    int   ec;              /* 0 on ok, else J error code */
    char *stdout_buf;      /* joined Joutput text (type 1/3/6). malloc'd */
    size_t stdout_len;
    char *stderr_buf;      /* joined Joutput text (type 2/4). malloc'd */
    size_t stderr_len;
    char *locale;          /* current locale after the call (strdup'd) */
    int   timed_out;       /* 1 if we had to interrupt due to deadline */
} eval_result;

void eval_result_free(eval_result *r);

/* One-time init: load libj from the given path (or NULL for default). */
int session_system_init(const char *libj_path, const char **err);

/* Create a named session. `sandbox` runs `(9!:25) 1` post-init.
 * `profile_path` is an optional path to profile.ijs; if NULL, profile is
 * skipped (foreigns and primitives still work; `load` etc. will not). */
session *session_create(const char *name, int sandbox, const char *profile_path,
                        const char **err);

/* Look up an existing session by name. Returns NULL if missing. */
session *session_lookup(const char *name);

/* Terminate: interrupts if busy, joins worker, frees J instance + struct. */
void session_terminate(const char *name);

/* List all sessions as a JSON array of objects. */
json *session_list_json(void);

/* Evaluate a sentence. Blocks until done or (if timeout_ms > 0) interrupts
 * and waits briefly for the worker to return. `out` is populated on success.
 * Returns 0 on ok, -1 on session-level error (sets *err). */
int session_eval(session *s, const char *sentence, int timeout_ms,
                 eval_result *out, const char **err);

/* Non-blocking interrupt of a running sentence. */
void session_break(session *s);

/* Accessors. */
const char *session_name(const session *s);
int session_is_sandbox(const session *s);

#endif
