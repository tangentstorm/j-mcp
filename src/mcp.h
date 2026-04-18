/* MCP JSON-RPC dispatch. Owns the tool registry for this server. */
#ifndef JMCP_MCP_H
#define JMCP_MCP_H

#include "json.h"

/* A tool handler: receives `arguments` (may be NULL) and the userdata the
 * tool was registered with. Returns the result value or NULL with *err_msg
 * set. Caller owns the returned json and frees it. */
typedef json *(*mcp_tool_fn)(const json *args, void *userdata, const char **err_msg);

typedef struct {
    const char *name;
    const char *description;
    const char *input_schema;   /* JSON Schema as a literal string */
    mcp_tool_fn handler;
} mcp_tool;

/* Register a built-in tool. The pointed-to struct must have static-lifetime
 * fields; they are referenced, not copied. No userdata, not unregisterable. */
void mcp_register(const mcp_tool *t);

/* Register a dynamic tool. Strings are copied and freed on unregister.
 * `userdata_free` (optional) is called when the tool is unregistered. */
void mcp_register_dyn(const char *name, const char *description,
                      const char *input_schema,
                      mcp_tool_fn handler,
                      void *userdata,
                      void (*userdata_free)(void *));

/* Remove a tool from the registry. Returns 1 if removed, 0 if not found or
 * the tool is a built-in (non-dynamic). */
int mcp_unregister(const char *name);

/* Main loop: read frames, dispatch, write replies, until EOF or shutdown. */
void mcp_run(void);

/* Notify clients that the tool list changed. */
void mcp_notify_tools_changed(void);

#endif
