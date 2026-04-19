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
    json_obj_set(o, "truncated", json_new_bool(r->truncated));
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
    case JMCP_JC2T: return 2;
    case JMCP_JC4T: return 4;
    default: return 0;
    }
}

/* ---------- UTF transcoding (used by char types) ---------- */

/* Encode one Unicode codepoint as UTF-8. Appends to (*buf, *n, *cap). */
static void utf8_emit(char **buf, size_t *n, size_t *cap, uint32_t cp) {
    if (*n + 4 + 1 > *cap) {
        size_t nc = *cap ? *cap : 32;
        while (nc < *n + 5) nc *= 2;
        *buf = realloc(*buf, nc);
        *cap = nc;
    }
    if      (cp < 0x80)    { (*buf)[(*n)++] = (char)cp; }
    else if (cp < 0x800)   { (*buf)[(*n)++] = (char)(0xC0 | (cp >> 6));
                             (*buf)[(*n)++] = (char)(0x80 | (cp & 0x3F)); }
    else if (cp < 0x10000) { (*buf)[(*n)++] = (char)(0xE0 | (cp >> 12));
                             (*buf)[(*n)++] = (char)(0x80 | ((cp >> 6) & 0x3F));
                             (*buf)[(*n)++] = (char)(0x80 | (cp & 0x3F)); }
    else                   { (*buf)[(*n)++] = (char)(0xF0 | (cp >> 18));
                             (*buf)[(*n)++] = (char)(0x80 | ((cp >> 12) & 0x3F));
                             (*buf)[(*n)++] = (char)(0x80 | ((cp >> 6) & 0x3F));
                             (*buf)[(*n)++] = (char)(0x80 | (cp & 0x3F)); }
    (*buf)[*n] = 0;
}

/* Convert UTF-16 code units (possibly with surrogates) to a malloc'd UTF-8
 * string. *out_len excludes the NUL. */
static char *utf16_to_utf8(const uint16_t *src, size_t n, size_t *out_len) {
    char *buf = NULL; size_t bn = 0, cap = 0;
    for (size_t i = 0; i < n; i++) {
        uint32_t cp = src[i];
        if (cp >= 0xD800 && cp <= 0xDBFF && i + 1 < n) {
            uint16_t lo = src[i + 1];
            if (lo >= 0xDC00 && lo <= 0xDFFF) {
                cp = 0x10000 + (((cp - 0xD800) << 10) | (lo - 0xDC00));
                i++;
            }
        }
        utf8_emit(&buf, &bn, &cap, cp);
    }
    if (!buf) { buf = calloc(1, 1); }
    if (out_len) *out_len = bn;
    return buf;
}

/* Convert UCS-4 codepoints to a malloc'd UTF-8 string. */
static char *utf32_to_utf8(const uint32_t *src, size_t n, size_t *out_len) {
    char *buf = NULL; size_t bn = 0, cap = 0;
    for (size_t i = 0; i < n; i++) utf8_emit(&buf, &bn, &cap, src[i]);
    if (!buf) { buf = calloc(1, 1); }
    if (out_len) *out_len = bn;
    return buf;
}

/* Decode one UTF-8 codepoint starting at s[0..n-1]. Returns bytes consumed,
 * or 0 on malformed input (caller should bail). */
static size_t utf8_decode(const unsigned char *s, size_t n, uint32_t *out) {
    if (!n) return 0;
    unsigned char c = s[0];
    if (c < 0x80) { *out = c; return 1; }
    if ((c & 0xE0) == 0xC0 && n >= 2) {
        *out = ((c & 0x1F) << 6) | (s[1] & 0x3F); return 2;
    }
    if ((c & 0xF0) == 0xE0 && n >= 3) {
        *out = ((c & 0x0F) << 12) | ((s[1] & 0x3F) << 6) | (s[2] & 0x3F); return 3;
    }
    if ((c & 0xF8) == 0xF0 && n >= 4) {
        *out = ((c & 0x07) << 18) | ((s[1] & 0x3F) << 12) |
               ((s[2] & 0x3F) << 6) | (s[3] & 0x3F); return 4;
    }
    return 0;
}

/* UTF-8 -> malloc'd UTF-16 code unit array. *out_units is the unit count. */
static uint16_t *utf8_to_utf16(const char *src, size_t n, size_t *out_units) {
    uint16_t *buf = NULL; size_t bn = 0, cap = 0;
    const unsigned char *p = (const unsigned char *)src;
    size_t rem = n;
    while (rem) {
        uint32_t cp;
        size_t used = utf8_decode(p, rem, &cp);
        if (!used) break;
        size_t need = (cp >= 0x10000) ? 2 : 1;
        if (bn + need > cap) {
            size_t nc = cap ? cap * 2 : 8;
            while (nc < bn + need) nc *= 2;
            buf = realloc(buf, nc * sizeof *buf);
            cap = nc;
        }
        if (cp >= 0x10000) {
            cp -= 0x10000;
            buf[bn++] = (uint16_t)(0xD800 | (cp >> 10));
            buf[bn++] = (uint16_t)(0xDC00 | (cp & 0x3FF));
        } else {
            buf[bn++] = (uint16_t)cp;
        }
        p += used; rem -= used;
    }
    if (!buf) buf = calloc(1, sizeof *buf);
    *out_units = bn;
    return buf;
}

/* UTF-8 -> malloc'd UTF-32 codepoint array. */
static uint32_t *utf8_to_utf32(const char *src, size_t n, size_t *out_units) {
    uint32_t *buf = NULL; size_t bn = 0, cap = 0;
    const unsigned char *p = (const unsigned char *)src;
    size_t rem = n;
    while (rem) {
        uint32_t cp;
        size_t used = utf8_decode(p, rem, &cp);
        if (!used) break;
        if (bn == cap) {
            cap = cap ? cap * 2 : 8;
            buf = realloc(buf, cap * sizeof *buf);
        }
        buf[bn++] = cp;
        p += used; rem -= used;
    }
    if (!buf) buf = calloc(1, sizeof *buf);
    *out_units = bn;
    return buf;
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
    case JMCP_JLIT:
    case JMCP_JC2T:
    case JMCP_JC4T: return "char";
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
        snprintf(buf, sizeof buf, "unsupported J type %lld; use j_eval instead",
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
        json_obj_set(o, "encoding", json_new_str("utf8"));
        json_obj_set(o, "data", json_new_strn(c.data_out ? (char *)c.data_out : "", n));
    } else if (c.type == JMCP_JC2T) {
        size_t utf8_len = 0;
        char *s = utf16_to_utf8((const uint16_t *)c.data_out, n, &utf8_len);
        json_obj_set(o, "encoding", json_new_str("utf16"));
        json_obj_set(o, "data", json_new_strn(s, utf8_len));
        free(s);
    } else if (c.type == JMCP_JC4T) {
        size_t utf8_len = 0;
        char *s = utf32_to_utf8((const uint32_t *)c.data_out, n, &utf8_len);
        json_obj_set(o, "encoding", json_new_str("utf32"));
        json_obj_set(o, "data", json_new_strn(s, utf8_len));
        free(s);
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

static int type_from_name(const char *n, const char *encoding, int64_t *t) {
    if (!strcmp(n, "int"))   { *t = JMCP_JINT; return 0; }
    if (!strcmp(n, "bool"))  { *t = JMCP_JB01; return 0; }
    if (!strcmp(n, "float")) { *t = JMCP_JFL;  return 0; }
    if (!strcmp(n, "char")) {
        if (!encoding || !*encoding || !strcmp(encoding, "utf8"))  { *t = JMCP_JLIT; return 0; }
        if (!strcmp(encoding, "utf16"))                             { *t = JMCP_JC2T; return 0; }
        if (!strcmp(encoding, "utf32"))                             { *t = JMCP_JC4T; return 0; }
        return -2;
    }
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
    const char *encoding = json_str(json_obj_get(args, "encoding"));

    int64_t type = 0;
    int tr = type_from_name(tname, encoding, &type);
    if (tr == -1) { *err = "type must be one of: int, char, bool, float"; return NULL; }
    if (tr == -2) { *err = "encoding must be one of: utf8, utf16, utf32"; return NULL; }

    const json *data_v  = json_obj_get(args, "data");
    const json *shape_v = json_obj_get(args, "shape");
    if (!data_v) { *err = "missing data"; return NULL; }

    /* Pre-transcode char data into the target J encoding. For LIT this is a
     * byte-for-byte copy; for C2T/C4T, UTF-8 -> UTF-16/UTF-32. `char_bytes`
     * (if non-NULL) holds the element bytes ready for JSetM. */
    void  *char_bytes = NULL;
    size_t char_n = 0;

    if ((type == JMCP_JLIT || type == JMCP_JC2T || type == JMCP_JC4T) &&
        data_v->t == JSON_STR) {
        const char *s = json_str(data_v);
        size_t slen = json_strlen(data_v);
        if (type == JMCP_JLIT) {
            char_n = slen;
            char_bytes = malloc(char_n ? char_n : 1);
            if (char_n) memcpy(char_bytes, s, char_n);
        } else if (type == JMCP_JC2T) {
            char_bytes = utf8_to_utf16(s, slen, &char_n);
        } else {
            char_bytes = utf8_to_utf32(s, slen, &char_n);
        }
    } else if (type == JMCP_JC2T || type == JMCP_JC4T) {
        *err = "char data with utf16/utf32 encoding must be a string";
        return NULL;
    }

    size_t rank = 0;
    int64_t *shape = NULL;
    size_t n_elems = 1;

    size_t data_len =
        char_bytes ? char_n :
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
    } else if (char_bytes || data_v->t == JSON_ARR) {
        /* Infer rank-1 from data length. `char_bytes` covers char-types-with-
         * string-input, whose effective length differs per encoding. */
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
    void *data_buf;
    if (char_bytes) {
        /* Reuse the already-transcoded buffer; no need to re-copy. */
        data_buf = char_bytes;
        char_bytes = NULL;
    } else {
        data_buf = calloc(n_elems ? n_elems : 1, esz ? esz : 1);
    }

    if (type == JMCP_JLIT && data_v->t == JSON_ARR) {
        /* LIT given as [int, int, ...] — take low byte of each. */
        unsigned char *p = data_buf;
        for (size_t i = 0; i < n_elems; i++)
            p[i] = (unsigned char)json_int(json_arr_at(data_v, i), 0);
    } else if (type == JMCP_JLIT || type == JMCP_JC2T || type == JMCP_JC4T) {
        /* Already filled by char_bytes. */
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
       "\"encoding\":{\"type\":\"string\",\"enum\":[\"utf8\",\"utf16\",\"utf32\"],\"description\":\"For type=char, pick which J char storage to allocate: utf8 (LIT, 1 byte per element), utf16 (C2T, 2 bytes), utf32 (C4T, 4 bytes). Default utf8. Input data is always a UTF-8 string; the server transcodes.\"},"
       "\"shape\":{\"type\":\"array\",\"items\":{\"type\":\"integer\",\"minimum\":0},\"description\":\"Dimensions. Omit for rank-0 scalar or rank-1 array inferred from data length. For char types, 'length' means element count in the chosen encoding, not UTF-8 byte count.\"},"
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

/* Tool names use underscores (not dots) because Anthropic's client validates
 * tool names against ^[a-zA-Z0-9_-]{1,64}$ and rejects servers whose tools
 * don't match. The MCP spec itself is more permissive. */
static const mcp_tool T_CREATE    = { "j_session_create",    "Create a named J session.",                 SCHEMA_CREATE,    tool_session_create };
static const mcp_tool T_LIST      = { "j_session_list",      "List live J sessions.",                     SCHEMA_EMPTY,     tool_session_list };
static const mcp_tool T_TERMINATE = { "j_session_terminate", "Terminate a J session.",                    SCHEMA_NAME_ONLY, tool_session_terminate };
static const mcp_tool T_RESTART   = { "j_session_restart",   "Terminate and recreate a session by name.", SCHEMA_NAME_ONLY, tool_session_restart };
static const mcp_tool T_EVAL      = { "j_eval",              "Evaluate a J sentence in a session.",       SCHEMA_EVAL,      tool_eval };
static const mcp_tool T_PARSE     = { "j_parse",             "Tokenize a sentence via ;:.",               SCHEMA_PARSE,     tool_parse };
static const mcp_tool T_BREAK     = { "j_break",             "Interrupt a running sentence.",             SCHEMA_NAME_ONLY, tool_break };
static const mcp_tool T_GET       = { "j_get",               "Read a J noun as a shape+type+data payload. Supports int / char / bool / float only; for other types use j_eval.", SCHEMA_GET, tool_get };
static const mcp_tool T_SET       = { "j_set",               "Create a J noun from a shape+type+data payload.", SCHEMA_SET, tool_set };

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
