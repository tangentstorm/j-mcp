/* MCP JSON-RPC dispatch. Owns the tool registry for this server. */
#ifndef JMCP_MCP_H
#define JMCP_MCP_H

#include "json.h"

/* A tool handler: receives `arguments` (may be NULL), returns the result value
 * or NULL with *err_msg set (caller frees result on success). */
typedef json *(*mcp_tool_fn)(const json *args, const char **err_msg);

typedef struct {
    const char *name;
    const char *description;
    const char *input_schema;   /* JSON Schema as a literal string */
    mcp_tool_fn handler;
} mcp_tool;

/* Register a static tool. Safe to call before mcp_run(). */
void mcp_register(const mcp_tool *t);

/* Main loop: read frames, dispatch, write replies, until EOF or shutdown. */
void mcp_run(void);

/* Called by tools that register/unregister tools at runtime. */
void mcp_notify_tools_changed(void);

#endif
