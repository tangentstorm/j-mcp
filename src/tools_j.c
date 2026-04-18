#include "tools_j.h"

#include "json.h"
#include "log.h"
#include "mcp.h"
#include "session.h"

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

static json *tool_session_create(const json *args, const char **err) {
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

static json *tool_session_list(const json *args, const char **err) {
    (void)args; (void)err;
    json *o = json_new_obj();
    json_obj_set(o, "sessions", session_list_json());
    return o;
}

static const char SCHEMA_EMPTY[] = "{\"type\":\"object\",\"properties\":{}}";

/* ---------- j.session.terminate ---------- */

static json *tool_session_terminate(const json *args, const char **err) {
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

static json *tool_eval(const json *args, const char **err) {
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

static json *tool_parse(const json *args, const char **err) {
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

/* ---------- j.break ---------- */

static json *tool_break(const json *args, const char **err) {
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

static const mcp_tool T_CREATE    = { "j.session.create",    "Create a named J session.",           SCHEMA_CREATE,    tool_session_create };
static const mcp_tool T_LIST      = { "j.session.list",      "List live J sessions.",               SCHEMA_EMPTY,     tool_session_list };
static const mcp_tool T_TERMINATE = { "j.session.terminate", "Terminate a J session.",              SCHEMA_NAME_ONLY, tool_session_terminate };
static const mcp_tool T_EVAL      = { "j.eval",              "Evaluate a J sentence in a session.", SCHEMA_EVAL,      tool_eval };
static const mcp_tool T_PARSE     = { "j.parse",             "Tokenize a sentence via ;:.",         SCHEMA_PARSE,     tool_parse };
static const mcp_tool T_BREAK     = { "j.break",             "Interrupt a running sentence.",       SCHEMA_NAME_ONLY, tool_break };

void tools_j_register(void) {
    mcp_register(&T_CREATE);
    mcp_register(&T_LIST);
    mcp_register(&T_TERMINATE);
    mcp_register(&T_EVAL);
    mcp_register(&T_PARSE);
    mcp_register(&T_BREAK);
}
