#include "session.h"
#include "jlib.h"
#include "log.h"

#include <errno.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>

static session_post_init_cb post_init_cb = NULL;
void session_set_post_init_cb(session_post_init_cb cb) { post_init_cb = cb; }

static char *dup_n(const char *s, size_t n) {
    char *d = malloc(n + 1);
    if (!d) return NULL;
    if (n) memcpy(d, s, n);
    d[n] = 0;
    return d;
}

/* ---------- session struct ---------- */

struct session {
    char *name;
    int   sandbox;
    JS    jt;

    pthread_t       worker;
    pthread_mutex_t mu;
    pthread_cond_t  cv_new_job;
    pthread_cond_t  cv_done;

    char *pending;    /* sentence to run next; owned; NULL = no job */
    void (*pending_cb)(struct session *, void *);
    void *pending_cb_arg;

    int   busy;       /* worker currently inside a job */
    int   shutdown;   /* ask worker to exit */

    int   last_ec;

    char *out_buf; size_t out_len, out_cap;
    char *err_buf; size_t err_len, err_cap;
    int   truncated;  /* 1 if the per-call output cap was hit */
};

#define MAX_SESSIONS 64

/* ---------- session registry + jt→session map ---------- */

static pthread_rwlock_t reg_lock = PTHREAD_RWLOCK_INITIALIZER;
static session *registry[MAX_SESSIONS];
static size_t   registry_n = 0;

/* Parallel map for callback dispatch (small, rwlock is fine). */
static session *find_by_jt(JS jt) {
    session *found = NULL;
    pthread_rwlock_rdlock(&reg_lock);
    for (size_t i = 0; i < registry_n; i++) {
        if (registry[i]->jt == jt) { found = registry[i]; break; }
    }
    pthread_rwlock_unlock(&reg_lock);
    return found;
}

/* ---------- J callbacks ---------- */

/* Append k bytes of s to the growable buffer, but do not exceed `limit`
 * total bytes stored. Returns 1 if any bytes were dropped, else 0. */
static int buf_append(char **b, size_t *n, size_t *cap, const char *s, size_t k,
                      size_t limit) {
    int dropped = 0;
    if (*n >= limit) return 1;
    if (*n + k > limit) { k = limit - *n; dropped = 1; }
    if (*n + k + 1 > *cap) {
        size_t nc = *cap ? *cap : 256;
        while (nc < *n + k + 1) nc *= 2;
        if (nc > limit + 1) nc = limit + 1;
        *b = realloc(*b, nc);
        *cap = nc;
    }
    if (k) memcpy(*b + *n, s, k);
    *n += k;
    (*b)[*n] = 0;
    return dropped;
}

static void j_output_cb(JS jt, int type, char *s) {
    if (!s) return;
    session *sess = find_by_jt(jt);
    if (!sess) return;
    size_t k = strlen(s);

    pthread_mutex_lock(&sess->mu);
    /* Share the cap across stdout + stderr so a call can't evade it. */
    size_t used = sess->out_len + sess->err_len;
    size_t room = used < SESSION_OUTPUT_CAP ? SESSION_OUTPUT_CAP - used : 0;
    int dropped = 0;
    if (type == JMCP_MTYOER || type == JMCP_MTYOSYS) {
        dropped = buf_append(&sess->err_buf, &sess->err_len, &sess->err_cap,
                             s, k, sess->err_len + room);
    } else if (type == JMCP_MTYOEXIT) {
        jlog("session %s: J requested exit (type %d)", sess->name, type);
    } else {
        dropped = buf_append(&sess->out_buf, &sess->out_len, &sess->out_cap,
                             s, k, sess->out_len + room);
    }
    if (dropped) sess->truncated = 1;
    pthread_mutex_unlock(&sess->mu);
}

static int j_wd_cb(JS jt, int x, void *parg, void **pres, char *loc) {
    (void)jt; (void)x; (void)parg; (void)loc;
    if (pres) *pres = NULL;
    return 0;
}

static char empty[1] = {0};
static char *j_input_cb(JS jt, char *prompt) {
    (void)jt; (void)prompt;
    return empty;
}

/* ---------- worker thread ---------- */

typedef struct {
    session *s;
    int init_done;          /* set to 1 once jt is assigned (or init failed) */
    int init_ok;
    pthread_mutex_t *init_mu;
    pthread_cond_t  *init_cv;
} worker_bootstrap;

static void *worker_main(void *arg) {
    worker_bootstrap *b = arg;
    session *s = b->s;

    /* JInit + JSM must happen on the thread that will subsequently call JDo.
     * J caches thread-local state in JInit that is only valid on the caller
     * thread. Signal the main thread once init is complete so session_create
     * can finish and return. */
    s->jt = jlib_new(j_output_cb, j_wd_cb, j_input_cb, JMCP_SMCON);

    pthread_mutex_lock(b->init_mu);
    b->init_ok   = s->jt != NULL;
    b->init_done = 1;
    pthread_cond_broadcast(b->init_cv);
    pthread_mutex_unlock(b->init_mu);

    if (!s->jt) return NULL;

    for (;;) {
        pthread_mutex_lock(&s->mu);
        while (!s->pending && !s->pending_cb && !s->shutdown)
            pthread_cond_wait(&s->cv_new_job, &s->mu);
        if (s->shutdown && !s->pending && !s->pending_cb) {
            pthread_mutex_unlock(&s->mu); return NULL;
        }

        char *sent = s->pending;
        void (*cb)(session *, void *) = s->pending_cb;
        void *cb_arg = s->pending_cb_arg;
        s->pending = NULL;
        s->pending_cb = NULL;
        s->pending_cb_arg = NULL;
        s->busy = 1;
        s->out_len = 0;
        s->err_len = 0;
        s->truncated = 0;
        if (s->out_buf) s->out_buf[0] = 0;
        if (s->err_buf) s->err_buf[0] = 0;
        pthread_mutex_unlock(&s->mu);

        int ec = 0;
        if (sent) {
            ec = jlib_do(s->jt, sent);
            free(sent);
        } else if (cb) {
            cb(s, cb_arg);
        }

        pthread_mutex_lock(&s->mu);
        s->last_ec = ec;
        s->busy = 0;
        pthread_cond_broadcast(&s->cv_done);
        pthread_mutex_unlock(&s->mu);
    }
}

/* ---------- system init ---------- */

int session_system_init(const char *libj_path, const char **err) {
    return jlib_load(libj_path, err);
}

/* ---------- create / destroy ---------- */

static void session_free(session *s) {
    free(s->name);
    free(s->pending);
    free(s->out_buf);
    free(s->err_buf);
    pthread_mutex_destroy(&s->mu);
    pthread_cond_destroy(&s->cv_new_job);
    pthread_cond_destroy(&s->cv_done);
    free(s);
}

session *session_create(const char *name, int sandbox, const char *profile_path,
                        const char **err) {
    if (registry_n >= MAX_SESSIONS) { *err = "too many sessions"; return NULL; }
    if (session_lookup(name))        { *err = "session already exists"; return NULL; }

    session *s = calloc(1, sizeof *s);
    s->name = strdup(name);
    s->sandbox = !!sandbox;
    pthread_mutex_init(&s->mu, NULL);
    pthread_cond_init(&s->cv_new_job, NULL);
    pthread_cond_init(&s->cv_done, NULL);

    /* Register BEFORE starting the worker so Joutput callbacks can resolve
     * the session via the jt the worker will publish. */
    pthread_rwlock_wrlock(&reg_lock);
    registry[registry_n++] = s;
    pthread_rwlock_unlock(&reg_lock);

    pthread_mutex_t init_mu = PTHREAD_MUTEX_INITIALIZER;
    pthread_cond_t  init_cv = PTHREAD_COND_INITIALIZER;
    worker_bootstrap b = { .s = s, .init_mu = &init_mu, .init_cv = &init_cv };

    if (pthread_create(&s->worker, NULL, worker_main, &b) != 0) {
        pthread_rwlock_wrlock(&reg_lock);
        if (registry_n && registry[registry_n-1] == s) registry_n--;
        pthread_rwlock_unlock(&reg_lock);
        session_free(s);
        *err = "pthread_create failed";
        return NULL;
    }

    pthread_mutex_lock(&init_mu);
    while (!b.init_done) pthread_cond_wait(&init_cv, &init_mu);
    pthread_mutex_unlock(&init_mu);
    if (!b.init_ok) {
        /* Worker already exited. Join + free. */
        pthread_join(s->worker, NULL);
        pthread_rwlock_wrlock(&reg_lock);
        for (size_t i = 0; i < registry_n; i++)
            if (registry[i] == s) {
                for (size_t j = i+1; j < registry_n; j++) registry[j-1] = registry[j];
                registry_n--; break;
            }
        pthread_rwlock_unlock(&reg_lock);
        session_free(s);
        *err = "JInit failed";
        return NULL;
    }

    /* Default to loading <libj_dir>/profile.ijs when the caller didn't pass
     * an explicit path. Without profile, z-locale utilities like `load`,
     * `require`, `smoutput`, addon paths, etc. are missing and the session
     * is effectively unusable for anything beyond raw primitives. */
    char default_profile[4096] = {0};
    const char *effective_profile = profile_path;
    if (!effective_profile || !*effective_profile) {
        const char *libdir = jlib_loaded_dir();
        if (libdir && *libdir) {
            char sep = strchr(libdir, '\\') ? '\\' : '/';
            snprintf(default_profile, sizeof default_profile,
                     "%s%cprofile.ijs", libdir, sep);
            struct stat st;
            if (stat(default_profile, &st) == 0) {
                effective_profile = default_profile;
            } else {
                jlog("session %s: no profile.ijs at %s; skipping profile",
                     name, default_profile);
            }
        }
    }

    if (effective_profile && *effective_profile) {
        /* Prime z-locale globals that profile.ijs expects. Run as separate
         * sentences: chained z-locative assignments in one sentence can
         * wedge subsequent evals. The `i.0 0` first-eval is a no-op that
         * warms up the interpreter. */
#if defined(_WIN32)
        const char *uname_lit = "'Win'";
#elif defined(__APPLE__)
        const char *uname_lit = "'Darwin'";
#else
        const char *uname_lit = "'Linux'";
#endif
        /* BINPATH = directory of the profile we're loading. J walks up from
         * BINPATH to find jlibrary's system/ and addons/, so it must be the
         * profile's own dir. In a packaged J install profile.ijs sits next
         * to libj, so this equals jlib_loaded_dir(); with an explicit
         * profile= override the user may point elsewhere. */
        char binpath[4096];
        snprintf(binpath, sizeof binpath, "%s", effective_profile);
        char *slash = strrchr(binpath, '/');
        char *bslash = strrchr(binpath, '\\');
        if (bslash && (!slash || bslash > slash)) slash = bslash;
        if (slash) *slash = 0; else binpath[0] = 0;

        char binpath_sent[4200];
        char *p = binpath_sent;
        p += sprintf(p, "BINPATH_z_=: '");
        for (const char *q = binpath; *q; q++) {
            if (*q == '\'') *p++ = '\'';
            *p++ = *q;
        }
        sprintf(p, "'");

        char uname_sent[64];
        snprintf(uname_sent, sizeof uname_sent, "UNAME_z_=: %s", uname_lit);

        const char *pre[] = {
            "i.0 0",
            binpath_sent,
            "ARGV_z_=: ,<'j-mcp'",
            uname_sent,
            "IFRASPI_z_=: 0",
            "RUNJSCRIPT_z_=: 0",
            "FHS_z_=: 0",
            "IFJMCP_z_=: 1",
        };
        for (size_t i = 0; i < sizeof pre / sizeof pre[0]; i++) {
            eval_result r = {0}; const char *e = NULL;
            session_eval(s, pre[i], 10000, &r, &e);
            eval_result_free(&r);
        }

        char buf[4200];
        int n = snprintf(buf, sizeof buf, "0!:0 <'%s'", effective_profile);
        if (n > 0 && (size_t)n < sizeof buf) {
            eval_result r = {0}; const char *e = NULL;
            session_eval(s, buf, 30000, &r, &e);
            if (r.ec) jlog("session %s: profile load ec=%d err=%s",
                           name, r.ec, r.stderr_buf ? r.stderr_buf : "");
            else     jlog("session %s: loaded profile %s",
                          name, effective_profile);
            eval_result_free(&r);
        }
    }
    if (sandbox) {
        eval_result r = {0};
        const char *e = NULL;
        session_eval(s, "(9!:25) 1", 10000, &r, &e);
        if (r.ec) jlog("session %s: sandbox enable ec=%d", name, r.ec);
        eval_result_free(&r);
    }

    jlog("session %s created (sandbox=%d)", name, sandbox);
    if (post_init_cb) post_init_cb(name);
    return s;
}

session *session_lookup(const char *name) {
    session *found = NULL;
    pthread_rwlock_rdlock(&reg_lock);
    for (size_t i = 0; i < registry_n; i++) {
        if (!strcmp(registry[i]->name, name)) { found = registry[i]; break; }
    }
    pthread_rwlock_unlock(&reg_lock);
    return found;
}

void session_terminate(const char *name) {
    session *s = NULL;
    pthread_rwlock_wrlock(&reg_lock);
    for (size_t i = 0; i < registry_n; i++) {
        if (!strcmp(registry[i]->name, name)) {
            s = registry[i];
            for (size_t j = i + 1; j < registry_n; j++) registry[j-1] = registry[j];
            registry_n--;
            break;
        }
    }
    pthread_rwlock_unlock(&reg_lock);
    if (!s) return;

    pthread_mutex_lock(&s->mu);
    s->shutdown = 1;
    pthread_cond_broadcast(&s->cv_new_job);
    pthread_mutex_unlock(&s->mu);

    if (s->busy) jlib_interrupt(s->jt);

    pthread_join(s->worker, NULL);
    jlib_free(s->jt);
    jlog("session %s terminated", s->name);
    session_free(s);
}

/* ---------- list ---------- */

json *session_list_json(void) {
    json *arr = json_new_arr();
    pthread_rwlock_rdlock(&reg_lock);
    for (size_t i = 0; i < registry_n; i++) {
        session *s = registry[i];
        json *o = json_new_obj();
        json_obj_set(o, "name", json_new_str(s->name));
        json_obj_set(o, "sandbox", json_new_bool(s->sandbox));

        pthread_mutex_lock(&s->mu);
        json_obj_set(o, "busy", json_new_bool(s->busy));
        pthread_mutex_unlock(&s->mu);

        json_arr_push(arr, o);
    }
    pthread_rwlock_unlock(&reg_lock);
    return arr;
}

/* ---------- eval ---------- */

void eval_result_free(eval_result *r) {
    if (!r) return;
    free(r->stdout_buf); r->stdout_buf = NULL;
    free(r->stderr_buf); r->stderr_buf = NULL;
    free(r->locale);     r->locale = NULL;
    r->stdout_len = r->stderr_len = 0;
    r->ec = 0; r->timed_out = 0; r->truncated = 0;
}

static void add_ns(struct timespec *ts, long ms) {
    ts->tv_sec  += ms / 1000;
    ts->tv_nsec += (ms % 1000) * 1000000L;
    if (ts->tv_nsec >= 1000000000L) { ts->tv_sec++; ts->tv_nsec -= 1000000000L; }
}

int session_eval(session *s, const char *sentence, int timeout_ms,
                 eval_result *out, const char **err) {
    pthread_mutex_lock(&s->mu);
    /* If the session is currently running another job, wait for it to
     * finish rather than failing. MCP clients routinely send back-to-back
     * tool calls on the same session; a spurious "busy" error would be a
     * protocol-level surprise. */
    while (s->busy || s->pending || s->pending_cb)
        pthread_cond_wait(&s->cv_done, &s->mu);
    s->pending = strdup(sentence);
    pthread_cond_signal(&s->cv_new_job);

    struct timespec deadline;
    int use_deadline = 0;
    if (timeout_ms > 0) {
        clock_gettime(CLOCK_REALTIME, &deadline);
        add_ns(&deadline, timeout_ms);
        use_deadline = 1;
    }

    int interrupted = 0;
    while (s->busy || s->pending) {
        int rc = 0;
        if (use_deadline) rc = pthread_cond_timedwait(&s->cv_done, &s->mu, &deadline);
        else              rc = pthread_cond_wait     (&s->cv_done, &s->mu);
        if (rc == ETIMEDOUT && !interrupted) {
            interrupted = 1;
            pthread_mutex_unlock(&s->mu);
            jlib_interrupt(s->jt);
            pthread_mutex_lock(&s->mu);
            /* keep waiting, without deadline, for worker to observe the break */
            use_deadline = 0;
        }
    }

    out->ec = s->last_ec;
    out->stdout_buf = dup_n(s->out_buf ? s->out_buf : "", s->out_len);
    out->stdout_len = s->out_len;
    out->stderr_buf = dup_n(s->err_buf ? s->err_buf : "", s->err_len);
    out->stderr_len = s->err_len;
    out->timed_out  = interrupted;
    out->truncated  = s->truncated;
    pthread_mutex_unlock(&s->mu);

    const char *loc = jlib_get_locale(s->jt);
    out->locale = strdup(loc ? loc : "");

    return 0;
}

void session_break(session *s) {
    if (!s) return;
    jlib_interrupt(s->jt);
}

void session_run_on_worker(session *s, void (*fn)(session *, void *), void *arg) {
    pthread_mutex_lock(&s->mu);
    while (s->busy || s->pending || s->pending_cb)
        pthread_cond_wait(&s->cv_done, &s->mu);
    s->pending_cb = fn;
    s->pending_cb_arg = arg;
    pthread_cond_signal(&s->cv_new_job);
    while (s->busy || s->pending_cb)
        pthread_cond_wait(&s->cv_done, &s->mu);
    pthread_mutex_unlock(&s->mu);
}

void *session_jt(session *s) { return s ? s->jt : NULL; }

const char *session_name(const session *s)  { return s ? s->name : NULL; }
int session_is_sandbox(const session *s)    { return s ? s->sandbox : 0; }
