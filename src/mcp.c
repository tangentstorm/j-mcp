#include "mcp.h"

#include "io.h"
#include "json.h"
#include "log.h"

#include <pthread.h>
#include <stdlib.h>
#include <string.h>

#define PROTOCOL_VERSION "2025-06-18"
#define SERVER_NAME      "j-mcp"
#define SERVER_VERSION   "0.1.0"

/* ---------- tool registry ---------- */

static pthread_rwlock_t reg_lock = PTHREAD_RWLOCK_INITIALIZER;
static const mcp_tool **tools = NULL;
static size_t tools_n = 0, tools_cap = 0;

void mcp_register(const mcp_tool *t) {
    pthread_rwlock_wrlock(&reg_lock);
    if (tools_n == tools_cap) {
        tools_cap = tools_cap ? tools_cap * 2 : 8;
        tools = realloc(tools, tools_cap * sizeof *tools);
    }
    tools[tools_n++] = t;
    pthread_rwlock_unlock(&reg_lock);
}

static const mcp_tool *find_tool(const char *name) {
    const mcp_tool *t = NULL;
    pthread_rwlock_rdlock(&reg_lock);
    for (size_t i = 0; i < tools_n; i++)
        if (!strcmp(tools[i]->name, name)) { t = tools[i]; break; }
    pthread_rwlock_unlock(&reg_lock);
    return t;
}

/* ---------- reply helpers ---------- */

static void send_json(json *msg) {
    size_t len;
    char *buf = json_emit(msg, &len);
    io_write_frame(buf, len);
    free(buf);
    json_free(msg);
}

/* Owns `id` and `result`. */
static void reply_ok(json *id, json *result) {
    json *m = json_new_obj();
    json_obj_set(m, "jsonrpc", json_new_str("2.0"));
    if (id) json_obj_set(m, "id", id); else json_obj_set(m, "id", json_new_null());
    json_obj_set(m, "result", result ? result : json_new_obj());
    send_json(m);
}

/* Owns `id`. */
static void reply_err(json *id, int code, const char *msg) {
    json *e = json_new_obj();
    json_obj_set(e, "code", json_new_int(code));
    json_obj_set(e, "message", json_new_str(msg));
    json *m = json_new_obj();
    json_obj_set(m, "jsonrpc", json_new_str("2.0"));
    if (id) json_obj_set(m, "id", id); else json_obj_set(m, "id", json_new_null());
    json_obj_set(m, "error", e);
    send_json(m);
}

/* Emit a notification (no id). */
static void notify(const char *method, json *params) {
    json *m = json_new_obj();
    json_obj_set(m, "jsonrpc", json_new_str("2.0"));
    json_obj_set(m, "method", json_new_str(method));
    if (params) json_obj_set(m, "params", params);
    send_json(m);
}

void mcp_notify_tools_changed(void) {
    notify("notifications/tools/list_changed", NULL);
}

/* Detach a value from its parent (by deep-copying). Simpler than tracking
 * ownership: when we need to return a field from a request as the response id,
 * we clone it. */
static json *clone(const json *v) {
    if (!v) return NULL;
    switch (v->t) {
    case JSON_NULL: return json_new_null();
    case JSON_BOOL: return json_new_bool(v->u.b);
    case JSON_INT:  return json_new_int(v->u.i);
    case JSON_NUM:  return json_new_num(v->u.n);
    case JSON_STR:  return json_new_strn(v->u.s.s, v->u.s.len);
    case JSON_ARR: {
        json *a = json_new_arr();
        for (size_t i = 0; i < v->u.a.n; i++) json_arr_push(a, clone(v->u.a.items[i]));
        return a;
    }
    case JSON_OBJ: {
        json *o = json_new_obj();
        for (size_t i = 0; i < v->u.o.n; i++) json_obj_set(o, v->u.o.keys[i], clone(v->u.o.vals[i]));
        return o;
    }
    }
    return json_new_null();
}

/* ---------- method handlers ---------- */

static json *h_initialize(const json *params) {
    (void)params;
    json *result = json_new_obj();
    json_obj_set(result, "protocolVersion", json_new_str(PROTOCOL_VERSION));

    json *caps = json_new_obj();
    json *tools_cap = json_new_obj();
    json_obj_set(tools_cap, "listChanged", json_new_bool(1));
    json_obj_set(caps, "tools", tools_cap);
    json_obj_set(result, "capabilities", caps);

    json *info = json_new_obj();
    json_obj_set(info, "name", json_new_str(SERVER_NAME));
    json_obj_set(info, "version", json_new_str(SERVER_VERSION));
    json_obj_set(result, "serverInfo", info);

    return result;
}

static json *h_tools_list(const json *params) {
    (void)params;
    json *result = json_new_obj();
    json *arr = json_new_arr();

    pthread_rwlock_rdlock(&reg_lock);
    for (size_t i = 0; i < tools_n; i++) {
        const mcp_tool *t = tools[i];
        json *tj = json_new_obj();
        json_obj_set(tj, "name", json_new_str(t->name));
        if (t->description) json_obj_set(tj, "description", json_new_str(t->description));
        if (t->input_schema) {
            const char *err = NULL;
            json *schema = json_parse(t->input_schema, strlen(t->input_schema), &err);
            if (schema) json_obj_set(tj, "inputSchema", schema);
            else {
                jlog("bad static schema for tool %s: %s", t->name, err ? err : "?");
                json_obj_set(tj, "inputSchema", json_new_obj());
            }
        } else {
            json *sch = json_new_obj();
            json_obj_set(sch, "type", json_new_str("object"));
            json_obj_set(tj, "inputSchema", sch);
        }
        json_arr_push(arr, tj);
    }
    pthread_rwlock_unlock(&reg_lock);

    json_obj_set(result, "tools", arr);
    return result;
}

static json *h_tools_call(const json *params, const char **err_msg, int *err_code) {
    const json *name_v = json_obj_get(params, "name");
    const char *name = json_str(name_v);
    if (!name) { *err_code = -32602; *err_msg = "missing tool name"; return NULL; }

    const mcp_tool *t = find_tool(name);
    if (!t) { *err_code = -32601; *err_msg = "unknown tool"; return NULL; }

    const json *args = json_obj_get(params, "arguments");
    const char *terr = NULL;
    json *tres = t->handler(args, &terr);

    /* Tool failure → MCP convention is `isError: true` inside the result so the
     * model can see it, rather than a JSON-RPC error. */
    json *result = json_new_obj();
    json *content = json_new_arr();
    if (!tres) {
        json *c = json_new_obj();
        json_obj_set(c, "type", json_new_str("text"));
        json_obj_set(c, "text", json_new_str(terr ? terr : "tool error"));
        json_arr_push(content, c);
        json_obj_set(result, "content", content);
        json_obj_set(result, "isError", json_new_bool(1));
    } else {
        /* If the tool returned a JSON object with "content"/"structuredContent",
         * treat it as a full MCP tool-result payload; otherwise serialise it
         * as one text block + structured. */
        if (tres->t == JSON_OBJ && json_obj_get(tres, "content")) {
            json_free(result);
            return tres;
        }
        size_t slen = 0;
        char *s = json_emit(tres, &slen);
        json *c = json_new_obj();
        json_obj_set(c, "type", json_new_str("text"));
        json_obj_set(c, "text", json_new_strn(s, slen));
        free(s);
        json_arr_push(content, c);
        json_obj_set(result, "content", content);
        json_obj_set(result, "structuredContent", tres);
    }
    return result;
}

/* ---------- dispatcher ---------- */

static int should_exit = 0;

static void dispatch(const json *msg) {
    const json *method_v = json_obj_get(msg, "method");
    const char *method = json_str(method_v);
    const json *id = json_obj_get(msg, "id");
    const json *params = json_obj_get(msg, "params");
    int is_request = (id != NULL);

    if (!method) {
        if (is_request) reply_err(clone(id), -32600, "missing method");
        return;
    }

    jlog("rpc %s %s", is_request ? "req" : "ntf", method);

    if (!strcmp(method, "initialize")) {
        reply_ok(clone(id), h_initialize(params));
        return;
    }
    if (!strcmp(method, "initialized") || !strcmp(method, "notifications/initialized")) {
        return; /* notification */
    }
    if (!strcmp(method, "ping")) {
        reply_ok(clone(id), json_new_obj());
        return;
    }
    if (!strcmp(method, "tools/list")) {
        reply_ok(clone(id), h_tools_list(params));
        return;
    }
    if (!strcmp(method, "tools/call")) {
        int ec = 0; const char *em = NULL;
        json *r = h_tools_call(params, &em, &ec);
        if (!r) reply_err(clone(id), ec, em);
        else    reply_ok(clone(id), r);
        return;
    }
    if (!strcmp(method, "shutdown")) {
        reply_ok(clone(id), json_new_obj());
        should_exit = 1;
        return;
    }
    if (!strcmp(method, "exit") || !strcmp(method, "notifications/cancelled")) {
        if (!strcmp(method, "exit")) should_exit = 1;
        return;
    }
    if (!strcmp(method, "$/cancelRequest")) {
        /* TODO: route to sessions once broker exists. */
        return;
    }

    if (is_request) reply_err(clone(id), -32601, "method not found");
}

void mcp_run(void) {
    for (;;) {
        if (should_exit) break;
        char *buf = NULL; size_t len = 0;
        int r = io_read_frame(&buf, &len);
        if (r == 1) { jlog("eof on stdin; shutting down"); break; }
        if (r < 0)  { jlog("read error; shutting down"); break; }

        const char *err = NULL;
        json *msg = json_parse(buf, len, &err);
        free(buf);
        if (!msg) { jlog("parse error: %s", err ? err : "?"); continue; }

        dispatch(msg);
        json_free(msg);
    }
}
