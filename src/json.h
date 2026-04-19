/* Minimal JSON parser/emitter for j-mcp. Self-contained. UTF-8 bytes in, out. */
#ifndef JMCP_JSON_H
#define JMCP_JSON_H

#include <stddef.h>

typedef enum {
    JSON_NULL,
    JSON_BOOL,
    JSON_INT,
    JSON_NUM,
    JSON_STR,
    JSON_ARR,
    JSON_OBJ
} json_type;

typedef struct json json;
struct json {
    json_type t;
    union {
        int b;
        long long i;
        double n;
        struct { char *s; size_t len; } s;
        struct { json **items; size_t n, cap; } a;
        struct { char **keys; size_t *klens; json **vals; size_t n, cap; } o;
    } u;
};

/* Parse. Returns NULL on error with *err pointing to a static message. */
json *json_parse(const char *buf, size_t len, const char **err);

/* Free a tree. Safe on NULL. */
void json_free(json *v);

/* Constructors. All strings are copied. */
json *json_new_null(void);
json *json_new_bool(int b);
json *json_new_int(long long i);
json *json_new_num(double n);
json *json_new_str(const char *s);
json *json_new_strn(const char *s, size_t n);
json *json_new_arr(void);
json *json_new_obj(void);

/* Mutation. `v` ownership transfers to the container. */
void json_arr_push(json *a, json *v);
void json_obj_set(json *o, const char *key, json *v);

/* Lookup. Returns NULL if key missing or wrong type. */
json *json_obj_get(const json *o, const char *key);
json *json_arr_at(const json *a, size_t i);
size_t json_arr_len(const json *a);

/* Typed accessors. Return defaults if type mismatches. */
const char *json_str(const json *v);       /* NUL-terminated or NULL */
size_t json_strlen(const json *v);
long long json_int(const json *v, long long dflt);
double json_num(const json *v, double dflt);
int json_bool(const json *v, int dflt);

/* Emit. Caller must free the returned buffer. *out_len excludes NUL. */
char *json_emit(const json *v, size_t *out_len);

#endif
