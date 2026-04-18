#include "tools_reg.h"

#include "json.h"
#include "log.h"
#include "mcp.h"
#include "session.h"

#include <ctype.h>
#include <errno.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>

#ifdef _WIN32
  #include <direct.h>
  #define PATH_SEP '\\'
#else
  #include <unistd.h>
  #define PATH_SEP '/'
#endif

/* ---------- in-memory registry ---------- */

typedef struct {
    char *name;            /* e.g. "square" */
    char *description;     /* optional */
    char *input_schema;    /* JSON Schema as a string */
    char *session;         /* backing session name */
    char *body;            /* J source that defines mcp_<name>_ */
} jtool;

static pthread_mutex_t mu = PTHREAD_MUTEX_INITIALIZER;
static jtool **tools = NULL;
static size_t  tools_n = 0, tools_cap = 0;
static char   *registry_path = NULL;

static void jtool_free(jtool *t) {
    if (!t) return;
    free(t->name); free(t->description); free(t->input_schema);
    free(t->session); free(t->body); free(t);
}

static jtool *jtool_find(const char *name) {
    for (size_t i = 0; i < tools_n; i++)
        if (!strcmp(tools[i]->name, name)) return tools[i];
    return NULL;
}

/* ---------- J-side helpers ---------- */

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

/* Evaluate `body` in the session and confirm mcp_<name>_ is a verb. */
static int install_verb(const char *session_name, const char *name,
                        const char *body, char *errbuf, size_t errcap) {
    session *s = session_lookup(session_name);
    if (!s) { snprintf(errbuf, errcap, "no session %s", session_name); return -1; }

    eval_result r = {0};
    const char *e = NULL;
    int rc = session_eval(s, body, 30000, &r, &e);
    if (rc < 0) {
        snprintf(errbuf, errcap, "eval: %s", e ? e : "error");
        eval_result_free(&r); return -1;
    }
    if (r.ec) {
        snprintf(errbuf, errcap, "body ec=%d: %.200s", r.ec,
                 r.stderr_buf && *r.stderr_buf ? r.stderr_buf : "");
        eval_result_free(&r); return -1;
    }
    eval_result_free(&r);

    char check[256];
    snprintf(check, sizeof check, "4!:0 <'mcp_%s_z_'", name);
    memset(&r, 0, sizeof r);
    rc = session_eval(s, check, 5000, &r, &e);
    int is_verb = (rc == 0 && !r.ec && r.stdout_buf && r.stdout_buf[0] == '3');
    eval_result_free(&r);
    if (!is_verb) {
        snprintf(errbuf, errcap, "body did not define mcp_%s_z_ as a verb", name);
        return -1;
    }
    return 0;
}

/* ---------- dynamic-tool handler ---------- */

static __thread char disp_errbuf[512];

static json *dispatch_j_verb(const json *args, void *ud, const char **err) {
    jtool *t = ud;
    session *s = session_lookup(t->session);
    if (!s) {
        snprintf(disp_errbuf, sizeof disp_errbuf,
                 "backing session %s is not live", t->session);
        *err = disp_errbuf;
        return NULL;
    }

    /* Encode args as JSON; use 'null' if missing. */
    size_t alen;
    char *ajson;
    if (args) ajson = json_emit(args, &alen);
    else      { ajson = strdup("null"); alen = 4; }

    char *escaped = j_escape_string(ajson);
    free(ajson);

    size_t n = strlen(t->name) + strlen(escaped) + 64;
    char *sent = malloc(n);
    /* 1!:2&2 writes to J file 2 (console stdout) which is routed through our
     * Joutput callback. Assigning to a local (=.) suppresses the automatic
     * top-level display of the returned value, which would otherwise echo
     * the result a second time. */
    snprintf(sent, n, "mcp_out_ =. (1!:2&2) (mcp_%s_z_) %s", t->name, escaped);
    free(escaped);

    eval_result r = {0};
    const char *e = NULL;
    int rc = session_eval(s, sent, 30000, &r, &e);
    free(sent);

    if (rc < 0) {
        snprintf(disp_errbuf, sizeof disp_errbuf, "eval: %s", e ? e : "error");
        *err = disp_errbuf;
        eval_result_free(&r); return NULL;
    }
    if (r.ec) {
        snprintf(disp_errbuf, sizeof disp_errbuf, "j ec=%d: %.300s", r.ec,
                 r.stderr_buf && *r.stderr_buf ? r.stderr_buf : "");
        *err = disp_errbuf;
        eval_result_free(&r); return NULL;
    }

    /* Trim trailing whitespace (safety, even though 1!:2&4 shouldn't add any). */
    while (r.stdout_len > 0) {
        char c = r.stdout_buf[r.stdout_len - 1];
        if (c == '\n' || c == '\r' || c == ' ' || c == '\t') {
            r.stdout_buf[--r.stdout_len] = 0;
        } else break;
    }

    const char *perr = NULL;
    json *v = json_parse(r.stdout_buf, r.stdout_len, &perr);
    if (!v) {
        /* Not JSON; wrap the raw text as a tool-result content block. */
        json *o = json_new_obj();
        json *content = json_new_arr();
        json *c = json_new_obj();
        json_obj_set(c, "type", json_new_str("text"));
        json_obj_set(c, "text", json_new_strn(r.stdout_buf, r.stdout_len));
        json_arr_push(content, c);
        json_obj_set(o, "content", content);
        v = o;
    }
    eval_result_free(&r);
    return v;
}

/* ---------- persistence ---------- */

static char *default_registry_path(void) {
    char buf[4096];
#ifdef _WIN32
    const char *ad = getenv("APPDATA");
    if (ad && *ad) snprintf(buf, sizeof buf, "%s\\j-mcp\\tools.json", ad);
    else           snprintf(buf, sizeof buf, "j-mcp-tools.json");
#else
    const char *xdg  = getenv("XDG_STATE_HOME");
    const char *home = getenv("HOME");
    if (xdg && *xdg)   snprintf(buf, sizeof buf, "%s/j-mcp/tools.json", xdg);
    else if (home)     snprintf(buf, sizeof buf, "%s/.local/state/j-mcp/tools.json", home);
    else               snprintf(buf, sizeof buf, "j-mcp-tools.json");
#endif
    return strdup(buf);
}

/* Recursive mkdir of the directory portion of `path`. */
static void mkdir_parents(const char *path) {
    char tmp[4096];
    snprintf(tmp, sizeof tmp, "%s", path);
    char *slash = strrchr(tmp, PATH_SEP);
    if (!slash) return;
    *slash = 0;
    for (char *p = tmp + 1; *p; p++) {
        if (*p == PATH_SEP) {
            *p = 0;
#ifdef _WIN32
            _mkdir(tmp);
#else
            mkdir(tmp, 0700);
#endif
            *p = PATH_SEP;
        }
    }
#ifdef _WIN32
    _mkdir(tmp);
#else
    mkdir(tmp, 0700);
#endif
}

static void save_locked(void) {
    if (!registry_path) return;
    json *root = json_new_obj();
    json_obj_set(root, "version", json_new_int(1));
    json *arr = json_new_arr();
    for (size_t i = 0; i < tools_n; i++) {
        jtool *t = tools[i];
        json *o = json_new_obj();
        json_obj_set(o, "name", json_new_str(t->name));
        if (t->description) json_obj_set(o, "description", json_new_str(t->description));
        /* inputSchema stored as an object, not a string. */
        const char *perr = NULL;
        json *sch = t->input_schema ? json_parse(t->input_schema, strlen(t->input_schema), &perr) : NULL;
        json_obj_set(o, "inputSchema", sch ? sch : json_new_obj());
        json_obj_set(o, "session", json_new_str(t->session));
        json_obj_set(o, "body", json_new_str(t->body));
        json_arr_push(arr, o);
    }
    json_obj_set(root, "tools", arr);

    size_t len;
    char *buf = json_emit(root, &len);
    json_free(root);

    mkdir_parents(registry_path);
    /* Atomic-ish: write to .tmp then rename. */
    char tmp[4096];
    snprintf(tmp, sizeof tmp, "%s.tmp", registry_path);
    FILE *f = fopen(tmp, "wb");
    if (!f) { jlog("tool registry: can't write %s: %s", tmp, strerror(errno)); free(buf); return; }
    fwrite(buf, 1, len, f);
    fclose(f);
#ifdef _WIN32
    remove(registry_path); /* rename() on Windows fails if dst exists */
#endif
    if (rename(tmp, registry_path) != 0)
        jlog("tool registry: rename failed: %s", strerror(errno));
    free(buf);
}

static void load_from_disk(void) {
    if (!registry_path) return;
    FILE *f = fopen(registry_path, "rb");
    if (!f) return;
    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (sz <= 0 || sz > 10 * 1024 * 1024) { fclose(f); return; }
    char *buf = malloc((size_t)sz + 1);
    size_t got = fread(buf, 1, (size_t)sz, f);
    buf[got] = 0;
    sz = (long)got;
    fclose(f);

    const char *perr = NULL;
    json *root = json_parse(buf, (size_t)sz, &perr);
    free(buf);
    if (!root) { jlog("tool registry: parse %s: %s", registry_path, perr ? perr : "?"); return; }

    const json *arr = json_obj_get(root, "tools");
    size_t n = json_arr_len(arr);
    for (size_t i = 0; i < n; i++) {
        const json *o = json_arr_at(arr, i);
        const char *name  = json_str(json_obj_get(o, "name"));
        const char *sess  = json_str(json_obj_get(o, "session"));
        const char *body  = json_str(json_obj_get(o, "body"));
        const char *desc  = json_str(json_obj_get(o, "description"));
        const json *sch   = json_obj_get(o, "inputSchema");
        if (!name || !sess || !body) continue;

        jtool *t = calloc(1, sizeof *t);
        t->name    = strdup(name);
        t->session = strdup(sess);
        t->body    = strdup(body);
        t->description = desc ? strdup(desc) : NULL;
        if (sch) t->input_schema = json_emit(sch, NULL);
        if (tools_n == tools_cap) {
            tools_cap = tools_cap ? tools_cap * 2 : 8;
            tools = realloc(tools, tools_cap * sizeof *tools);
        }
        tools[tools_n++] = t;

        /* Register in MCP right away so tools/list reflects persisted tools
         * even before their backing session is (re)created. The dispatch
         * handler checks session liveness at call time. */
        mcp_register_dyn(t->name, t->description, t->input_schema,
                         dispatch_j_verb, t, NULL);
    }
    json_free(root);
    jlog("tool registry: loaded %zu tool(s) from %s", tools_n, registry_path);
}

/* ---------- session replay hook ---------- */

static void on_session_created(const char *name) {
    pthread_mutex_lock(&mu);
    for (size_t i = 0; i < tools_n; i++) {
        jtool *t = tools[i];
        if (strcmp(t->session, name) != 0) continue;
        char errbuf[512]; errbuf[0] = 0;
        if (install_verb(t->session, t->name, t->body, errbuf, sizeof errbuf) < 0)
            jlog("tool replay: %s -> %s: %s", name, t->name, errbuf);
        else
            jlog("tool replay: %s -> %s installed", name, t->name);
    }
    pthread_mutex_unlock(&mu);
}

/* ---------- MCP handlers ---------- */

static int valid_name(const char *n) {
    if (!n || !*n) return 0;
    if (!isalpha((unsigned char)n[0])) return 0;
    for (const char *p = n; *p; p++)
        if (!(isalnum((unsigned char)*p) || *p == '_')) return 0;
    if (strlen(n) > 64) return 0;
    return 1;
}

static json *h_tool_register(const json *args, void *ud, const char **err) {
    (void)ud;
    const char *name = json_str(json_obj_get(args, "name"));
    const char *sess = json_str(json_obj_get(args, "session"));
    const char *body = json_str(json_obj_get(args, "body"));
    const char *desc = json_str(json_obj_get(args, "description"));
    const json *sch  = json_obj_get(args, "inputSchema");

    if (!valid_name(name))   { *err = "invalid name (must match [a-zA-Z][a-zA-Z0-9_]{0,63})"; return NULL; }
    if (!sess || !*sess)     { *err = "missing session";   return NULL; }
    if (!body || !*body)     { *err = "missing body";      return NULL; }

    pthread_mutex_lock(&mu);
    if (jtool_find(name)) {
        pthread_mutex_unlock(&mu);
        *err = "tool already registered; unregister first";
        return NULL;
    }

    static char errbuf[512];
    if (install_verb(sess, name, body, errbuf, sizeof errbuf) < 0) {
        pthread_mutex_unlock(&mu);
        *err = errbuf;
        return NULL;
    }

    jtool *t = calloc(1, sizeof *t);
    t->name    = strdup(name);
    t->session = strdup(sess);
    t->body    = strdup(body);
    t->description = desc ? strdup(desc) : NULL;
    t->input_schema = sch ? json_emit(sch, NULL) : strdup("{\"type\":\"object\"}");

    if (tools_n == tools_cap) {
        tools_cap = tools_cap ? tools_cap * 2 : 8;
        tools = realloc(tools, tools_cap * sizeof *tools);
    }
    tools[tools_n++] = t;

    mcp_register_dyn(t->name, t->description, t->input_schema,
                     dispatch_j_verb, t, NULL);
    save_locked();
    pthread_mutex_unlock(&mu);

    mcp_notify_tools_changed();

    json *o = json_new_obj();
    json_obj_set(o, "ok",   json_new_bool(1));
    json_obj_set(o, "name", json_new_str(name));
    return o;
}

static json *h_tool_unregister(const json *args, void *ud, const char **err) {
    (void)ud;
    const char *name = json_str(json_obj_get(args, "name"));
    if (!name) { *err = "missing name"; return NULL; }

    pthread_mutex_lock(&mu);
    jtool *found = NULL; size_t fi = 0;
    for (size_t i = 0; i < tools_n; i++)
        if (!strcmp(tools[i]->name, name)) { found = tools[i]; fi = i; break; }
    if (!found) { pthread_mutex_unlock(&mu); *err = "no such registered tool"; return NULL; }

    mcp_unregister(name);
    for (size_t j = fi + 1; j < tools_n; j++) tools[j-1] = tools[j];
    tools_n--;
    jtool_free(found);
    save_locked();
    pthread_mutex_unlock(&mu);

    mcp_notify_tools_changed();

    json *o = json_new_obj();
    json_obj_set(o, "ok", json_new_bool(1));
    return o;
}

static json *h_tool_list(const json *args, void *ud, const char **err) {
    (void)args; (void)ud; (void)err;
    json *arr = json_new_arr();
    pthread_mutex_lock(&mu);
    for (size_t i = 0; i < tools_n; i++) {
        jtool *t = tools[i];
        json *o = json_new_obj();
        json_obj_set(o, "name", json_new_str(t->name));
        if (t->description) json_obj_set(o, "description", json_new_str(t->description));
        json_obj_set(o, "session", json_new_str(t->session));
        json_obj_set(o, "body",    json_new_str(t->body));
        json_arr_push(arr, o);
    }
    pthread_mutex_unlock(&mu);

    json *res = json_new_obj();
    json_obj_set(res, "tools", arr);
    return res;
}

/* ---------- init ---------- */

static const char SCHEMA_REGISTER[] =
    "{\"type\":\"object\","
     "\"required\":[\"name\",\"session\",\"body\"],"
     "\"properties\":{"
       "\"name\":{\"type\":\"string\",\"description\":\"Tool name (matches [a-zA-Z][a-zA-Z0-9_]{0,63}). The J verb bound to it must be named mcp_<name>_z_ (z locale).\"},"
       "\"description\":{\"type\":\"string\",\"description\":\"Human-readable description shown to clients.\"},"
       "\"inputSchema\":{\"type\":\"object\",\"description\":\"JSON Schema for the tool's arguments.\"},"
       "\"session\":{\"type\":\"string\",\"description\":\"Name of an existing J session that owns the verb.\"},"
       "\"body\":{\"type\":\"string\",\"description\":\"J source that defines a monadic verb named mcp_<name>_z_. The verb receives a JSON string and should return a JSON string.\"}"
     "}}";

static const char SCHEMA_NAME_ONLY[] =
    "{\"type\":\"object\",\"required\":[\"name\"],"
     "\"properties\":{\"name\":{\"type\":\"string\"}}}";

static const char SCHEMA_EMPTY[] = "{\"type\":\"object\",\"properties\":{}}";

static const mcp_tool T_REGISTER =
    { "j.tool.register",
      "Register a new MCP tool whose implementation is a J verb in a session.",
      SCHEMA_REGISTER,
      NULL };   /* handler patched below */

static const mcp_tool T_UNREGISTER =
    { "j.tool.unregister", "Drop a previously registered J-defined tool.",
      SCHEMA_NAME_ONLY, NULL };

static const mcp_tool T_LIST =
    { "j.tool.list", "List user-registered J-defined tools.",
      SCHEMA_EMPTY, NULL };

/* Static wrappers that forward to our user-data-taking handlers. */
static json *reg_w(const json *a, void *u, const char **e) { return h_tool_register(a, u, e); }
static json *unreg_w(const json *a, void *u, const char **e) { return h_tool_unregister(a, u, e); }
static json *list_w(const json *a, void *u, const char **e) { return h_tool_list(a, u, e); }

void tools_reg_init(void) {
    registry_path = default_registry_path();
    jlog("tool registry: %s", registry_path);

    /* Register built-in meta-tools. We stash a tiny dynamic entry just to get
     * userdata plumbing; but since they aren't user-registered we register them
     * as static ones with mcp_register, then add a small forwarder. */
    mcp_tool r = T_REGISTER;   r.handler = reg_w;    mcp_register(&r);
    mcp_tool u = T_UNREGISTER; u.handler = unreg_w;  mcp_register(&u);
    mcp_tool l = T_LIST;       l.handler = list_w;   mcp_register(&l);

    session_set_post_init_cb(on_session_created);
    load_from_disk();
}
