#include "json.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ---------- allocation helpers ---------- */

static void *xmalloc(size_t n) {
    void *p = malloc(n);
    if (!p) { fprintf(stderr, "j-mcp: out of memory\n"); abort(); }
    return p;
}

static void *xcalloc(size_t n, size_t sz) {
    void *p = calloc(n, sz);
    if (!p) { fprintf(stderr, "j-mcp: out of memory\n"); abort(); }
    return p;
}

static void *xrealloc(void *p, size_t n) {
    void *q = realloc(p, n);
    if (!q) { fprintf(stderr, "j-mcp: out of memory\n"); abort(); }
    return q;
}

/* ---------- constructors ---------- */

json *json_new_null(void) {
    json *v = xcalloc(1, sizeof *v);
    v->t = JSON_NULL;
    return v;
}

json *json_new_bool(int b) {
    json *v = xcalloc(1, sizeof *v);
    v->t = JSON_BOOL; v->u.b = !!b;
    return v;
}

json *json_new_int(long long i) {
    json *v = xcalloc(1, sizeof *v);
    v->t = JSON_INT; v->u.i = i;
    return v;
}

json *json_new_num(double n) {
    json *v = xcalloc(1, sizeof *v);
    v->t = JSON_NUM; v->u.n = n;
    return v;
}

json *json_new_strn(const char *s, size_t n) {
    json *v = xcalloc(1, sizeof *v);
    v->t = JSON_STR;
    v->u.s.s = xmalloc(n + 1);
    if (n) memcpy(v->u.s.s, s, n);
    v->u.s.s[n] = 0;
    v->u.s.len = n;
    return v;
}

json *json_new_str(const char *s) { return json_new_strn(s, strlen(s)); }

json *json_new_arr(void) {
    json *v = xcalloc(1, sizeof *v);
    v->t = JSON_ARR;
    return v;
}

json *json_new_obj(void) {
    json *v = xcalloc(1, sizeof *v);
    v->t = JSON_OBJ;
    return v;
}

/* ---------- free ---------- */

void json_free(json *v) {
    if (!v) return;
    switch (v->t) {
    case JSON_STR:
        free(v->u.s.s);
        break;
    case JSON_ARR:
        for (size_t i = 0; i < v->u.a.n; i++) json_free(v->u.a.items[i]);
        free(v->u.a.items);
        break;
    case JSON_OBJ:
        for (size_t i = 0; i < v->u.o.n; i++) {
            free(v->u.o.keys[i]);
            json_free(v->u.o.vals[i]);
        }
        free(v->u.o.keys);
        free(v->u.o.klens);
        free(v->u.o.vals);
        break;
    default:
        break;
    }
    free(v);
}

/* ---------- mutation ---------- */

void json_arr_push(json *a, json *v) {
    if (a->u.a.n == a->u.a.cap) {
        a->u.a.cap = a->u.a.cap ? a->u.a.cap * 2 : 4;
        a->u.a.items = xrealloc(a->u.a.items, a->u.a.cap * sizeof *a->u.a.items);
    }
    a->u.a.items[a->u.a.n++] = v;
}

void json_obj_set(json *o, const char *key, json *v) {
    size_t klen = strlen(key);
    for (size_t i = 0; i < o->u.o.n; i++) {
        if (o->u.o.klens[i] == klen && !memcmp(o->u.o.keys[i], key, klen)) {
            json_free(o->u.o.vals[i]);
            o->u.o.vals[i] = v;
            return;
        }
    }
    if (o->u.o.n == o->u.o.cap) {
        o->u.o.cap = o->u.o.cap ? o->u.o.cap * 2 : 4;
        o->u.o.keys  = xrealloc(o->u.o.keys,  o->u.o.cap * sizeof *o->u.o.keys);
        o->u.o.klens = xrealloc(o->u.o.klens, o->u.o.cap * sizeof *o->u.o.klens);
        o->u.o.vals  = xrealloc(o->u.o.vals,  o->u.o.cap * sizeof *o->u.o.vals);
    }
    char *kcopy = xmalloc(klen + 1);
    memcpy(kcopy, key, klen); kcopy[klen] = 0;
    o->u.o.keys[o->u.o.n]  = kcopy;
    o->u.o.klens[o->u.o.n] = klen;
    o->u.o.vals[o->u.o.n]  = v;
    o->u.o.n++;
}

/* ---------- accessors ---------- */

json *json_obj_get(const json *o, const char *key) {
    if (!o || o->t != JSON_OBJ) return NULL;
    size_t klen = strlen(key);
    for (size_t i = 0; i < o->u.o.n; i++)
        if (o->u.o.klens[i] == klen && !memcmp(o->u.o.keys[i], key, klen))
            return o->u.o.vals[i];
    return NULL;
}

json *json_arr_at(const json *a, size_t i) {
    if (!a || a->t != JSON_ARR || i >= a->u.a.n) return NULL;
    return a->u.a.items[i];
}

size_t json_arr_len(const json *a) {
    return (a && a->t == JSON_ARR) ? a->u.a.n : 0;
}

const char *json_str(const json *v) {
    return (v && v->t == JSON_STR) ? v->u.s.s : NULL;
}

size_t json_strlen(const json *v) {
    return (v && v->t == JSON_STR) ? v->u.s.len : 0;
}

long long json_int(const json *v, long long dflt) {
    if (!v) return dflt;
    if (v->t == JSON_INT) return v->u.i;
    if (v->t == JSON_NUM) return (long long)v->u.n;
    if (v->t == JSON_BOOL) return v->u.b;
    return dflt;
}

double json_num(const json *v, double dflt) {
    if (!v) return dflt;
    if (v->t == JSON_NUM) return v->u.n;
    if (v->t == JSON_INT) return (double)v->u.i;
    return dflt;
}

int json_bool(const json *v, int dflt) {
    if (!v) return dflt;
    if (v->t == JSON_BOOL) return v->u.b;
    if (v->t == JSON_INT)  return v->u.i != 0;
    return dflt;
}

/* ---------- parser ---------- */

typedef struct {
    const char *p;
    const char *end;
    const char *err;
} P;

static void skip_ws(P *p) {
    while (p->p < p->end) {
        char c = *p->p;
        if (c == ' ' || c == '\t' || c == '\n' || c == '\r') p->p++;
        else break;
    }
}

static int p_eof(const P *p) { return p->p >= p->end; }

static json *p_value(P *p);

/* Decode a \uXXXX escape into *cp (codepoint). Advances p->p past the 4 hex. */
static int p_hex4(P *p, unsigned *cp) {
    if (p->end - p->p < 4) return 0;
    unsigned v = 0;
    for (int i = 0; i < 4; i++) {
        char c = p->p[i];
        v <<= 4;
        if      (c >= '0' && c <= '9') v |= (unsigned)(c - '0');
        else if (c >= 'a' && c <= 'f') v |= (unsigned)(c - 'a' + 10);
        else if (c >= 'A' && c <= 'F') v |= (unsigned)(c - 'A' + 10);
        else return 0;
    }
    p->p += 4;
    *cp = v;
    return 1;
}

static void buf_push(char **b, size_t *n, size_t *cap, char c) {
    if (*n + 1 >= *cap) {
        *cap = *cap ? *cap * 2 : 16;
        *b = xrealloc(*b, *cap);
    }
    (*b)[(*n)++] = c;
}

static void buf_push_utf8(char **b, size_t *n, size_t *cap, unsigned cp) {
    if (cp < 0x80) {
        buf_push(b, n, cap, (char)cp);
    } else if (cp < 0x800) {
        buf_push(b, n, cap, (char)(0xC0 | (cp >> 6)));
        buf_push(b, n, cap, (char)(0x80 | (cp & 0x3F)));
    } else if (cp < 0x10000) {
        buf_push(b, n, cap, (char)(0xE0 | (cp >> 12)));
        buf_push(b, n, cap, (char)(0x80 | ((cp >> 6) & 0x3F)));
        buf_push(b, n, cap, (char)(0x80 | (cp & 0x3F)));
    } else {
        buf_push(b, n, cap, (char)(0xF0 | (cp >> 18)));
        buf_push(b, n, cap, (char)(0x80 | ((cp >> 12) & 0x3F)));
        buf_push(b, n, cap, (char)(0x80 | ((cp >> 6) & 0x3F)));
        buf_push(b, n, cap, (char)(0x80 | (cp & 0x3F)));
    }
}

static json *p_string(P *p) {
    if (p_eof(p) || *p->p != '"') { p->err = "expected string"; return NULL; }
    p->p++;
    char *buf = NULL; size_t n = 0, cap = 0;
    while (!p_eof(p)) {
        unsigned char c = (unsigned char)*p->p;
        if (c == '"') {
            p->p++;
            buf_push(&buf, &n, &cap, 0); /* NUL */
            json *v = xcalloc(1, sizeof *v);
            v->t = JSON_STR;
            v->u.s.s = buf ? buf : xmalloc(1);
            if (!buf) v->u.s.s[0] = 0;
            v->u.s.len = n ? n - 1 : 0;
            return v;
        }
        if (c == '\\') {
            p->p++;
            if (p_eof(p)) { p->err = "bad escape"; free(buf); return NULL; }
            char e = *p->p++;
            switch (e) {
            case '"':  buf_push(&buf, &n, &cap, '"');  break;
            case '\\': buf_push(&buf, &n, &cap, '\\'); break;
            case '/':  buf_push(&buf, &n, &cap, '/');  break;
            case 'b':  buf_push(&buf, &n, &cap, '\b'); break;
            case 'f':  buf_push(&buf, &n, &cap, '\f'); break;
            case 'n':  buf_push(&buf, &n, &cap, '\n'); break;
            case 'r':  buf_push(&buf, &n, &cap, '\r'); break;
            case 't':  buf_push(&buf, &n, &cap, '\t'); break;
            case 'u': {
                unsigned cp;
                if (!p_hex4(p, &cp)) { p->err = "bad \\u"; free(buf); return NULL; }
                if (cp >= 0xD800 && cp <= 0xDBFF) {
                    if (p->end - p->p < 2 || p->p[0] != '\\' || p->p[1] != 'u') {
                        p->err = "unpaired surrogate"; free(buf); return NULL;
                    }
                    p->p += 2;
                    unsigned cp2;
                    if (!p_hex4(p, &cp2) || cp2 < 0xDC00 || cp2 > 0xDFFF) {
                        p->err = "bad surrogate pair"; free(buf); return NULL;
                    }
                    cp = 0x10000 + (((cp - 0xD800) << 10) | (cp2 - 0xDC00));
                }
                buf_push_utf8(&buf, &n, &cap, cp);
                break;
            }
            default:
                p->err = "bad escape"; free(buf); return NULL;
            }
        } else if (c < 0x20) {
            p->err = "unescaped control in string"; free(buf); return NULL;
        } else {
            buf_push(&buf, &n, &cap, (char)c);
            p->p++;
        }
    }
    p->err = "unterminated string"; free(buf); return NULL;
}

static json *p_number(P *p) {
    const char *start = p->p;
    int is_float = 0;
    if (*p->p == '-') p->p++;
    while (!p_eof(p) && isdigit((unsigned char)*p->p)) p->p++;
    if (!p_eof(p) && *p->p == '.') { is_float = 1; p->p++;
        while (!p_eof(p) && isdigit((unsigned char)*p->p)) p->p++; }
    if (!p_eof(p) && (*p->p == 'e' || *p->p == 'E')) { is_float = 1; p->p++;
        if (!p_eof(p) && (*p->p == '+' || *p->p == '-')) p->p++;
        while (!p_eof(p) && isdigit((unsigned char)*p->p)) p->p++; }
    size_t len = (size_t)(p->p - start);
    if (!len) { p->err = "bad number"; return NULL; }
    char tmp[64];
    if (len >= sizeof tmp) { p->err = "number too long"; return NULL; }
    memcpy(tmp, start, len); tmp[len] = 0;
    if (is_float) {
        char *endp; double d = strtod(tmp, &endp);
        if (*endp) { p->err = "bad number"; return NULL; }
        return json_new_num(d);
    } else {
        char *endp; long long i = strtoll(tmp, &endp, 10);
        if (*endp) { p->err = "bad number"; return NULL; }
        return json_new_int(i);
    }
}

static int p_expect(P *p, const char *lit) {
    size_t n = strlen(lit);
    if ((size_t)(p->end - p->p) < n) return 0;
    if (memcmp(p->p, lit, n)) return 0;
    p->p += n;
    return 1;
}

static json *p_array(P *p) {
    p->p++; /* [ */
    json *a = json_new_arr();
    skip_ws(p);
    if (!p_eof(p) && *p->p == ']') { p->p++; return a; }
    for (;;) {
        skip_ws(p);
        json *v = p_value(p);
        if (!v) { json_free(a); return NULL; }
        json_arr_push(a, v);
        skip_ws(p);
        if (p_eof(p)) { p->err = "unterminated array"; json_free(a); return NULL; }
        if (*p->p == ',') { p->p++; continue; }
        if (*p->p == ']') { p->p++; return a; }
        p->err = "expected , or ]"; json_free(a); return NULL;
    }
}

static json *p_object(P *p) {
    p->p++; /* { */
    json *o = json_new_obj();
    skip_ws(p);
    if (!p_eof(p) && *p->p == '}') { p->p++; return o; }
    for (;;) {
        skip_ws(p);
        json *k = p_string(p);
        if (!k) { json_free(o); return NULL; }
        skip_ws(p);
        if (p_eof(p) || *p->p != ':') { p->err = "expected :"; json_free(k); json_free(o); return NULL; }
        p->p++;
        skip_ws(p);
        json *v = p_value(p);
        if (!v) { json_free(k); json_free(o); return NULL; }
        json_obj_set(o, k->u.s.s, v);
        json_free(k);
        skip_ws(p);
        if (p_eof(p)) { p->err = "unterminated object"; json_free(o); return NULL; }
        if (*p->p == ',') { p->p++; continue; }
        if (*p->p == '}') { p->p++; return o; }
        p->err = "expected , or }"; json_free(o); return NULL;
    }
}

static json *p_value(P *p) {
    skip_ws(p);
    if (p_eof(p)) { p->err = "unexpected eof"; return NULL; }
    char c = *p->p;
    if (c == '"') return p_string(p);
    if (c == '{') return p_object(p);
    if (c == '[') return p_array(p);
    if (c == '-' || (c >= '0' && c <= '9')) return p_number(p);
    if (c == 't') { if (p_expect(p, "true"))  return json_new_bool(1); p->err = "bad literal"; return NULL; }
    if (c == 'f') { if (p_expect(p, "false")) return json_new_bool(0); p->err = "bad literal"; return NULL; }
    if (c == 'n') { if (p_expect(p, "null"))  return json_new_null();  p->err = "bad literal"; return NULL; }
    p->err = "unexpected character";
    return NULL;
}

json *json_parse(const char *buf, size_t len, const char **err) {
    P p = { buf, buf + len, NULL };
    json *v = p_value(&p);
    if (!v) { if (err) *err = p.err ? p.err : "parse error"; return NULL; }
    skip_ws(&p);
    if (!p_eof(&p)) { if (err) *err = "trailing garbage"; json_free(v); return NULL; }
    if (err) *err = NULL;
    return v;
}

/* ---------- emitter ---------- */

typedef struct { char *b; size_t n, cap; } EB;

static void eb_putc(EB *e, char c) {
    if (e->n + 1 >= e->cap) { e->cap = e->cap ? e->cap * 2 : 64; e->b = xrealloc(e->b, e->cap); }
    e->b[e->n++] = c;
}

static void eb_puts(EB *e, const char *s, size_t n) {
    if (e->n + n + 1 >= e->cap) {
        size_t nc = e->cap ? e->cap : 64;
        while (nc < e->n + n + 1) nc *= 2;
        e->cap = nc; e->b = xrealloc(e->b, e->cap);
    }
    memcpy(e->b + e->n, s, n);
    e->n += n;
}

/* Return the length (1..4) of a valid UTF-8 sequence starting at s[0..n-1],
 * or 0 if the bytes there are not a valid sequence. Enforces the overlong
 * and surrogate restrictions from RFC 3629. */
static int utf8_valid_seq(const unsigned char *s, size_t n) {
    if (!n) return 0;
    unsigned char c = s[0];
    if (c < 0x80) return 1;
    if (c < 0xC2) return 0;                                 /* lone continuation or overlong 2-byte */
    if (c < 0xE0) {
        if (n < 2) return 0;
        if ((s[1] & 0xC0) != 0x80) return 0;
        return 2;
    }
    if (c < 0xF0) {
        if (n < 3) return 0;
        if ((s[1] & 0xC0) != 0x80 || (s[2] & 0xC0) != 0x80) return 0;
        if (c == 0xE0 && s[1] < 0xA0) return 0;             /* overlong */
        if (c == 0xED && s[1] >= 0xA0) return 0;            /* surrogate */
        return 3;
    }
    if (c < 0xF5) {
        if (n < 4) return 0;
        if ((s[1] & 0xC0) != 0x80 || (s[2] & 0xC0) != 0x80 || (s[3] & 0xC0) != 0x80) return 0;
        if (c == 0xF0 && s[1] < 0x90) return 0;             /* overlong */
        if (c == 0xF4 && s[1] >= 0x90) return 0;            /* > U+10FFFF */
        return 4;
    }
    return 0;
}

/* JSON strings must be valid UTF-8 per RFC 8259. J's output (1!:2, smoutput,
 * auto-display) emits raw bytes that are frequently Latin-1 or otherwise
 * non-UTF-8 (e.g. `a.`, J's 256-byte alphabet). Pass through valid UTF-8
 * sequences unchanged; escape stray bytes >=0x80 as \u00XX so the receiver
 * sees a well-formed, losslessly-reinterpreted Latin-1 codepoint. */
static void eb_str(EB *e, const char *s, size_t n) {
    eb_putc(e, '"');
    size_t i = 0;
    while (i < n) {
        unsigned char c = (unsigned char)s[i];
        switch (c) {
        case '"':  eb_puts(e, "\\\"", 2); i++; continue;
        case '\\': eb_puts(e, "\\\\", 2); i++; continue;
        case '\b': eb_puts(e, "\\b",  2); i++; continue;
        case '\f': eb_puts(e, "\\f",  2); i++; continue;
        case '\n': eb_puts(e, "\\n",  2); i++; continue;
        case '\r': eb_puts(e, "\\r",  2); i++; continue;
        case '\t': eb_puts(e, "\\t",  2); i++; continue;
        }
        if (c < 0x20) {
            char buf[8]; int m = snprintf(buf, sizeof buf, "\\u%04x", c);
            eb_puts(e, buf, (size_t)m);
            i++;
        } else if (c < 0x80) {
            eb_putc(e, (char)c);
            i++;
        } else {
            int k = utf8_valid_seq((const unsigned char *)s + i, n - i);
            if (k > 0) {
                eb_puts(e, s + i, (size_t)k);
                i += (size_t)k;
            } else {
                char buf[8]; int m = snprintf(buf, sizeof buf, "\\u%04x", c);
                eb_puts(e, buf, (size_t)m);
                i++;
            }
        }
    }
    eb_putc(e, '"');
}

static void emit(EB *e, const json *v) {
    if (!v) { eb_puts(e, "null", 4); return; }
    switch (v->t) {
    case JSON_NULL: eb_puts(e, "null", 4); break;
    case JSON_BOOL: eb_puts(e, v->u.b ? "true" : "false", v->u.b ? 4 : 5); break;
    case JSON_INT: {
        char buf[32]; int m = snprintf(buf, sizeof buf, "%lld", v->u.i);
        eb_puts(e, buf, (size_t)m); break;
    }
    case JSON_NUM: {
        char buf[32]; int m = snprintf(buf, sizeof buf, "%.17g", v->u.n);
        eb_puts(e, buf, (size_t)m); break;
    }
    case JSON_STR: eb_str(e, v->u.s.s, v->u.s.len); break;
    case JSON_ARR:
        eb_putc(e, '[');
        for (size_t i = 0; i < v->u.a.n; i++) {
            if (i) eb_putc(e, ',');
            emit(e, v->u.a.items[i]);
        }
        eb_putc(e, ']');
        break;
    case JSON_OBJ:
        eb_putc(e, '{');
        for (size_t i = 0; i < v->u.o.n; i++) {
            if (i) eb_putc(e, ',');
            eb_str(e, v->u.o.keys[i], v->u.o.klens[i]);
            eb_putc(e, ':');
            emit(e, v->u.o.vals[i]);
        }
        eb_putc(e, '}');
        break;
    }
}

char *json_emit(const json *v, size_t *out_len) {
    EB e = { NULL, 0, 0 };
    emit(&e, v);
    eb_putc(&e, 0);
    if (out_len) *out_len = e.n - 1;
    return e.b;
}
