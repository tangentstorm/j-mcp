/* Registry of user-defined MCP tools whose bodies are J verbs. */
#ifndef JMCP_TOOLS_REG_H
#define JMCP_TOOLS_REG_H

/* Load persisted registry, install the built-in meta-tools
 * (j.tool.register, j.tool.unregister, j.tool.list), and hook into session
 * creation so stored tools are replayed into their backing sessions. */
void tools_reg_init(void);

#endif
