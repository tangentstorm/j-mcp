#include "tools_j.h"

#include "jlib.h"
#include "json.h"
#include "log.h"
#include "mcp.h"
#include "session.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ---------- helpers ---------- */

static const char *req_str(const json *args, const char *key, const char **err) {
    const json *v = json_obj_get(args, key);
    const char *s = json_str(v);
    if (!s) { *err = "missing required string argument"; return NULL; }
    return s;
}

/* Build a J string literal: wrap in single quotes, double any interior '. */
static char *j_escape_string(const char *s) {
    size_t n = strlen(s);
    char *out = malloc(n * 2 + 3);
    char *p = out;
    *p++ = '\'';
    for (size_t i = 0; i < n; i++) {
        if (s[i] == '\'') { *p++ = '\''; *p++ = '\''; }
        else *p++ = s[i];
    }
    *p++ = '\'';
    *p = 0;
    return out;
}

/* Build the standard eval-result object used by j.eval and j.parse. */
static json *eval_result_to_json(const eval_result *r) {
    json *o = json_new_obj();
    json_obj_set(o, "ec",       json_new_int(r->ec));
    json_obj_set(o, "stdout",   json_new_strn(r->stdout_buf ? r->stdout_buf : "",
                                              r->stdout_len));
    json_obj_set(o, "stderr",   json_new_strn(r->stderr_buf ? r->stderr_buf : "",
                                              r->stderr_len));
    json_obj_set(o, "locale",   json_new_str(r->locale ? r->locale : ""));
    json_obj_set(o, "timedOut", json_new_bool(r->timed_out));
    return o;
}

/* ---------- j.session.create ---------- */

static json *tool_session_create(const json *args, void *userdata, const char **err) {
    (void)userdata;
    const char *name = req_str(args, "name", err);
    if (!name) return NULL;
    int sandbox = json_bool(json_obj_get(args, "sandbox"), 0);
    const char *profile = json_str(json_obj_get(args, "profile"));

    const char *e = NULL;
    session *s = session_create(name, sandbox, profile, &e);
    if (!s) { *err = e ? e : "create failed"; return NULL; }

    json *o = json_new_obj();
    json_obj_set(o, "name", json_new_str(name));
    json_obj_set(o, "sandbox", json_new_bool(sandbox));
    return o;
}

static const char SCHEMA_CREATE[] =
    "{\"type\":\"object\","
     "\"required\":[\"name\"],"
     "\"properties\":{"
       "\"name\":{\"type\":\"string\",\"description\":\"Unique session name.\"},"
       "\"sandbox\":{\"type\":\"boolean\",\"description\":\"Enable J security level (9!:25 1). Blocks shell escapes and restricts file loads to .ijs/.js. Monotonic.\"},"
       "\"profile\":{\"type\":\"string\",\"description\":\"Optional absolute path to profile.ijs to load at startup.\"}"
     "}}";

/* ---------- j.session.list ---------- */

static json *tool_session_list(const json *args, void *userdata, const char **err) {
    (void)userdata;
    (void)args; (void)err;
    json *o = json_new_obj();
    json_obj_set(o, "sessions", session_list_json());
    return o;
}

static const char SCHEMA_EMPTY[] = "{\"type\":\"object\",\"properties\":{}}";

/* ---------- j.session.terminate ---------- */

static json *tool_session_terminate(const json *args, void *userdata, const char **err) {
    (void)userdata;
    const char *name = req_str(args, "name", err);
    if (!name) return NULL;
    if (!session_lookup(name)) { *err = "no such session"; return NULL; }
    session_terminate(name);
    json *o = json_new_obj();
    json_obj_set(o, "ok", json_new_bool(1));
    return o;
}

static const char SCHEMA_NAME_ONLY[] =
    "{\"type\":\"object\",\"required\":[\"name\"],"
     "\"properties\":{\"name\":{\"type\":\"string\"}}}";

/* ---------- j.eval ---------- */

static json *tool_eval(const json *args, void *userdata, const char **err) {
    (void)userdata;
    const char *name = req_str(args, "name", err);
    if (!name) return NULL;
    const char *sentence = req_str(args, "sentence", err);
    if (!sentence) return NULL;
    int timeout_ms = (int)json_int(json_obj_get(args, "timeoutMs"), 0);

    session *s = session_lookup(name);
    if (!s) { *err = "no such session"; return NULL; }

    eval_result r = {0};
    const char *e = NULL;
    int rc = session_eval(s, sentence, timeout_ms, &r, &e);
    if (rc < 0) { *err = e ? e : "eval failed"; eval_result_free(&r); return NULL; }

    json *out = eval_result_to_json(&r);
    eval_result_free(&r);
    return out;
}

static const char SCHEMA_EVAL[] =
    "{\"type\":\"object\","
     "\"required\":[\"name\",\"sentence\"],"
     "\"properties\":{"
       "\"name\":{\"type\":\"string\"},"
       "\"sentence\":{\"type\":\"string\",\"description\":\"A J sentence to evaluate.\"},"
       "\"timeoutMs\":{\"type\":\"integer\",\"description\":\"Deadline after which the sentence is interrupted. 0 or missing = no limit.\"}"
     "}}";

/* ---------- j.parse ---------- */

static json *tool_parse(const json *args, void *userdata, const char **err) {
    (void)userdata;
    const char *name = req_str(args, "name", err);
    if (!name) return NULL;
    const char *sentence = req_str(args, "sentence", err);
    if (!sentence) return NULL;
    session *s = session_lookup(name);
    if (!s) { *err = "no such session"; return NULL; }

    char *lit = j_escape_string(sentence);
    size_t n = strlen(lit) + 16;
    char *expr = malloc(n);
    snprintf(expr, n, ";: %s", lit);
    free(lit);

    eval_result r = {0};
    const char *e = NULL;
    int rc = session_eval(s, expr, 10000, &r, &e);
    free(expr);
    if (rc < 0) { *err = e ? e : "eval failed"; eval_result_free(&r); return NULL; }

    json *out = eval_result_to_json(&r);
    eval_result_free(&r);
    return out;
}

static const char SCHEMA_PARSE[] =
    "{\"type\":\"object\","
     "\"required\":[\"name\",\"sentence\"],"
     "\"properties\":{"
       "\"name\":{\"type\":\"string\"},"
       "\"sentence\":{\"type\":\"string\",\"description\":\"Sentence to tokenize via ;:.\"}"
     "}}";

/* ---------- j.session.restart ---------- */

static json *tool_session_restart(const json *args, void *userdata, const char **err) {
    (void)userdata;
    const char *name = req_str(args, "name", err);
    if (!name) return NULL;
    session *s = session_lookup(name);
    if (!s) { *err = "no such session"; return NULL; }

    int sandbox = session_is_sandbox(s);
    /* We don't preserve profile_path across restarts yet; document and skip. */

    session_terminate(name);

    const char *e = NULL;
    session *ns = session_create(name, sandbox, NULL, &e);
    if (!ns) { *err = e ? e : "recreate failed"; return NULL; }

    json *o = json_new_obj();
    json_obj_set(o, "name",    json_new_str(name));
    json_obj_set(o, "sandbox", json_new_bool(sandbox));
    return o;
}

/* ---------- j.get ---------- */

typedef struct {
    const char *var;
    int ec;
    int64_t type, rank;
    int64_t *shape_out;   /* malloc'd copy */
    void    *data_out;    /* malloc'd copy */
    size_t   data_bytes;
} get_ctx;

static size_t type_elem_size(int64_t t) {
    switch (t) {
    case JMCP_JB01: return 1;
    case JMCP_JLIT: return 1;
    case JMCP_JINT: return 8;
    case JMCP_JFL:  return 8;
    default: return 0;
    }
}

static void get_worker(session *s, void *arg) {
    get_ctx *c = arg;
    int64_t shape_ptr = 0, data_ptr = 0;
    c->ec = jlib_get_m(session_jt(s), c->var,
                       &c->type, &c->rank, &shape_ptr, &data_ptr);
    if (c->ec) return;

    size_t rank = (size_t)c->rank;
    size_t n = 1;
    int64_t *sh = (int64_t *)(uintptr_t)shape_ptr;
    for (size_t i = 0; i < rank; i++) n *= (size_t)sh[i];

    size_t esz = type_elem_size(c->type);
    /* Copy out of J's memory — the returned pointers are transient. */
    if (rank) {
        c->shape_out = malloc(rank * sizeof(int64_t));
        memcpy(c->shape_out, sh, rank * sizeof(int64_t));
    }
    if (esz && n) {
        c->data_bytes = n * esz;
        c->data_out = malloc(c->data_bytes);
        memcpy(c->data_out, (void *)(uintptr_t)data_ptr, c->data_bytes);
    }
}

static const char *type_name(int64_t t) {
    switch (t) {
    case JMCP_JB01: return "bool";
    case JMCP_JLIT: return "char";
    case JMCP_JINT: return "int";
    case JMCP_JFL:  return "float";
    default: return NULL;
    }
}

static json *tool_get(const json *args, void *userdata, const char **err) {
    (void)userdata;
    const char *name = req_str(args, "name", err);
    if (!name) return NULL;
    const char *var = req_str(args, "var", err);
    if (!var) return NULL;
    session *s = session_lookup(name);
    if (!s) { *err = "no such session"; return NULL; }

    get_ctx c = { .var = var };
    session_run_on_worker(s, get_worker, &c);

    if (c.ec) {
        static char buf[96];
        snprintf(buf, sizeof buf, "JGetM ec=%d", c.ec);
        *err = buf;
        free(c.shape_out); free(c.data_out);
        return NULL;
    }

    const char *tn = type_name(c.type);
    if (!tn) {
        static char buf[96];
        snprintf(buf, sizeof buf, "unsupported J type %lld; use j.eval instead",
                 (long long)c.type);
        *err = buf;
        free(c.shape_out); free(c.data_out);
        return NULL;
    }

    json *o = json_new_obj();
    json_obj_set(o, "type", json_new_str(tn));
    json_obj_set(o, "rank", json_new_int(c.rank));

    json *sh = json_new_arr();
    size_t n = 1;
    for (size_t i = 0; i < (size_t)c.rank; i++) {
        int64_t d = c.shape_out[i];
        json_arr_push(sh, json_new_int(d));
        n *= (size_t)d;
    }
    json_obj_set(o, "shape", sh);

    if (c.type == JMCP_JLIT) {
        /* Characters → return as a string (bytes as-is). */
        json_obj_set(o, "data", json_new_strn(c.data_out ? (char *)c.data_out : "", n));
    } else if (c.type == JMCP_JB01) {
        json *d = json_new_arr();
        const unsigned char *p = c.data_out;
        for (size_t i = 0; i < n; i++) json_arr_push(d, json_new_bool(p[i]));
        json_obj_set(o, "data", d);
    } else if (c.type == JMCP_JINT) {
        json *d = json_new_arr();
        const int64_t *p = c.data_out;
        for (size_t i = 0; i < n; i++) json_arr_push(d, json_new_int(p[i]));
        json_obj_set(o, "data", d);
    } else {
        json *d = json_new_arr();
        const double *p = c.data_out;
        for (size_t i = 0; i < n; i++) json_arr_push(d, json_new_num(p[i]));
        json_obj_set(o, "data", d);
    }

    free(c.shape_out);
    free(c.data_out);
    return o;
}

static const char SCHEMA_GET[] =
    "{\"type\":\"object\","
     "\"required\":[\"name\",\"var\"],"
     "\"properties\":{"
       "\"name\":{\"type\":\"string\",\"description\":\"Session name.\"},"
       "\"var\":{\"type\":\"string\",\"description\":\"J noun name in the session's current locale.\"}"
     "}}";

/* ---------- j.set ---------- */

typedef struct {
    const char *var;
    int64_t type, rank;
    int64_t *shape;
    void    *data;
    int ec;
} set_ctx;

static void set_worker(session *s, void *arg) {
    set_ctx *c = arg;
    int64_t shape_i = (int64_t)(uintptr_t)c->shape;
    int64_t data_i  = (int64_t)(uintptr_t)c->data;
    c->ec = jlib_set_m(session_jt(s), c->var,
                       &c->type, &c->rank, &shape_i, &data_i);
}

static int type_from_name(const char *n, int64_t *t) {
    if (!strcmp(n, "int"))   { *t = JMCP_JINT; return 0; }
    if (!strcmp(n, "char"))  { *t = JMCP_JLIT; return 0; }
    if (!strcmp(n, "bool"))  { *t = JMCP_JB01; return 0; }
    if (!strcmp(n, "float")) { *t = JMCP_JFL;  return 0; }
    return -1;
}

static json *tool_set(const json *args, void *userdata, const char **err) {
    (void)userdata;
    const char *name = req_str(args, "name", err);
    if (!name) return NULL;
    const char *var = req_str(args, "var", err);
    if (!var) return NULL;
    const char *tname = req_str(args, "type", err);
    if (!tname) return NULL;

    int64_t type = 0;
    if (type_from_name(tname, &type) < 0) {
        *err = "type must be one of: int, char, bool, float";
        return NULL;
    }

    const json *data_v  = json_obj_get(args, "data");
    const json *shape_v = json_obj_get(args, "shape");
    if (!data_v) { *err = "missing data"; return NULL; }

    /* Shape: explicit array, or inferred from data length (rank 1) or 0
     * (scalar from a single value / one-char string). */
    size_t rank = 0;
    int64_t *shape = NULL;
    size_t n_elems = 1;

    /* Decide n_elems based on data. */
    size_t data_len =
        (type == JMCP_JLIT && data_v->t == JSON_STR) ? json_strlen(data_v) :
        (data_v->t == JSON_ARR) ? json_arr_len(data_v) : 1;

    if (shape_v && shape_v->t == JSON_ARR) {
        rank = json_arr_len(shape_v);
        shape = calloc(rank ? rank : 1, sizeof *shape);
        n_elems = 1;
        for (size_t i = 0; i < rank; i++) {
            int64_t d = json_int(json_arr_at(shape_v, i), -1);
            if (d < 0) { free(shape); *err = "shape must be non-negative ints"; return NULL; }
            shape[i] = d;
            n_elems *= (size_t)d;
        }
        if (n_elems != data_len) {
            free(shape);
            static char buf[128];
            snprintf(buf, sizeof buf, "shape product %zu != data length %zu",
                     n_elems, data_len);
            *err = buf;
            return NULL;
        }
    } else if (data_v->t == JSON_ARR ||
               (type == JMCP_JLIT && data_v->t == JSON_STR)) {
        rank = 1;
        shape = malloc(sizeof *shape);
        shape[0] = (int64_t)data_len;
        n_elems = data_len;
    } else {
        /* Scalar. rank=0, no shape array, 1 element. */
        rank = 0;
        n_elems = 1;
    }

    size_t esz = type_elem_size(type);
    void *data_buf = calloc(n_elems ? n_elems : 1, esz ? esz : 1);

    if (type == JMCP_JLIT) {
        const char *s =
            data_v->t == JSON_STR ? json_str(data_v) :
            NULL;
        if (s) {
            memcpy(data_buf, s, n_elems);
        } else if (data_v->t == JSON_ARR) {
            /* Char array given as [int, int, ...] — take low byte of each. */
            unsigned char *p = data_buf;
            for (size_t i = 0; i < n_elems; i++)
                p[i] = (unsigned char)json_int(json_arr_at(data_v, i), 0);
        }
    } else if (type == JMCP_JB01) {
        unsigned char *p = data_buf;
        if (data_v->t == JSON_ARR) {
            for (size_t i = 0; i < n_elems; i++)
                p[i] = (unsigned char)(json_bool(json_arr_at(data_v, i), 0) ? 1 : 0);
        } else {
            p[0] = (unsigned char)(json_bool(data_v, 0) ? 1 : 0);
        }
    } else if (type == JMCP_JINT) {
        int64_t *p = data_buf;
        if (data_v->t == JSON_ARR) {
            for (size_t i = 0; i < n_elems; i++)
                p[i] = (int64_t)json_int(json_arr_at(data_v, i), 0);
        } else {
            p[0] = (int64_t)json_int(data_v, 0);
        }
    } else {
        double *p = data_buf;
        if (data_v->t == JSON_ARR) {
            for (size_t i = 0; i < n_elems; i++)
                p[i] = json_num(json_arr_at(data_v, i), 0.0);
        } else {
            p[0] = json_num(data_v, 0.0);
        }
    }

    session *s = session_lookup(name);
    if (!s) { free(shape); free(data_buf); *err = "no such session"; return NULL; }

    set_ctx c = {
        .var = var, .type = type, .rank = (int64_t)rank,
        .shape = shape, .data = data_buf,
    };
    session_run_on_worker(s, set_worker, &c);

    free(shape);
    free(data_buf);

    if (c.ec) {
        static char buf[96];
        snprintf(buf, sizeof buf, "JSetM ec=%d", c.ec);
        *err = buf;
        return NULL;
    }

    json *o = json_new_obj();
    json_obj_set(o, "ok", json_new_bool(1));
    return o;
}

static const char SCHEMA_SET[] =
    "{\"type\":\"object\","
     "\"required\":[\"name\",\"var\",\"type\",\"data\"],"
     "\"properties\":{"
       "\"name\":{\"type\":\"string\"},"
       "\"var\":{\"type\":\"string\",\"description\":\"J noun name to create.\"},"
       "\"type\":{\"type\":\"string\",\"enum\":[\"int\",\"char\",\"bool\",\"float\"]},"
       "\"shape\":{\"type\":\"array\",\"items\":{\"type\":\"integer\",\"minimum\":0},\"description\":\"Dimensions. Omit for rank-0 scalar or rank-1 array inferred from data length.\"},"
       "\"data\":{\"description\":\"Array of values, or a string for type=char. A single value for a scalar.\"}"
     "}}";

/* ---------- j.break ---------- */

static json *tool_break(const json *args, void *userdata, const char **err) {
    (void)userdata;
    const char *name = req_str(args, "name", err);
    if (!name) return NULL;
    session *s = session_lookup(name);
    if (!s) { *err = "no such session"; return NULL; }
    session_break(s);
    json *o = json_new_obj();
    json_obj_set(o, "ok", json_new_bool(1));
    return o;
}

/* ---------- registry ---------- */

static const mcp_tool T_CREATE    = { "j.session.create",    "Create a named J session.",                 SCHEMA_CREATE,    tool_session_create };
static const mcp_tool T_LIST      = { "j.session.list",      "List live J sessions.",                     SCHEMA_EMPTY,     tool_session_list };
static const mcp_tool T_TERMINATE = { "j.session.terminate", "Terminate a J session.",                    SCHEMA_NAME_ONLY, tool_session_terminate };
static const mcp_tool T_RESTART   = { "j.session.restart",   "Terminate and recreate a session by name.", SCHEMA_NAME_ONLY, tool_session_restart };
static const mcp_tool T_EVAL      = { "j.eval",              "Evaluate a J sentence in a session.",       SCHEMA_EVAL,      tool_eval };
static const mcp_tool T_PARSE     = { "j.parse",             "Tokenize a sentence via ;:.",               SCHEMA_PARSE,     tool_parse };
static const mcp_tool T_BREAK     = { "j.break",             "Interrupt a running sentence.",             SCHEMA_NAME_ONLY, tool_break };
static const mcp_tool T_GET       = { "j.get",               "Read a J noun as a shape+type+data payload. Supports int / char / bool / float only; for other types use j.eval.", SCHEMA_GET, tool_get };
static const mcp_tool T_SET       = { "j.set",               "Create a J noun from a shape+type+data payload.", SCHEMA_SET, tool_set };

void tools_j_register(void) {
    mcp_register(&T_CREATE);
    mcp_register(&T_LIST);
    mcp_register(&T_TERMINATE);
    mcp_register(&T_RESTART);
    mcp_register(&T_EVAL);
    mcp_register(&T_PARSE);
    mcp_register(&T_BREAK);
    mcp_register(&T_GET);
    mcp_register(&T_SET);
}
