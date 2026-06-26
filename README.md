# j-mcp — MCP server for the J programming language

A lightweight [Model Context Protocol](https://modelcontextprotocol.io) server
that embeds the J interpreter via `libj.{so,dylib,dll}` and exposes it to LLM
clients.

Written in **C11** with no dependencies beyond libc, pthreads, and libdl.
Intended to be contributable upstream into
[jsource](https://github.com/jsoftware/jsource) once the surface stabilises.

## Demo video

<iframe width="560" height="315" src="https://www.youtube.com/embed/l0lJxHoII-M?si=UimNEzOnmGiBs_Av" title="YouTube video player" frameborder="0" allow="accelerometer; autoplay; clipboard-write; encrypted-media; gyroscope; picture-in-picture; web-share" referrerpolicy="strict-origin-when-cross-origin" allowfullscreen></iframe>

## Provided Tools

| Tool | Purpose |
|---|---|
| `j_session_create` | Spawn a named J session (`sandbox: bool`). |
| `j_session_list` | Enumerate live sessions. |
| `j_session_terminate` | Destroy a named session. |
| `j_session_restart` | Terminate + recreate with same options. |
| `j_eval` | Evaluate a J sentence; structured result + captured output. |
| `j_parse` | Tokenize a sentence via `;:` and return the word list. |
| `j_break` | Interrupt a running sentence (non-blocking; uses `JInterrupt`). |
| `j_get` / `j_set` | Exchange dense numeric/char arrays via `JGetM`/`JSetM`. |
| `j_tool_register` | Register a new MCP tool whose body is a J verb. |
| `j_tool_unregister` | Drop a registered tool. |
| `j_tool_list` | Show registered J-defined tools. |

### Registered J-defined tools

An LLM (or human) can register new MCP tools whose implementation is a J verb
bound to a particular session. The body is persisted to
`$XDG_STATE_HOME/j-mcp/tools.json` and replayed into the session when it is
(re)created.

## Install

`j-mcp` works with any MCP-compatible client. Quickstart guides:

- **Claude Desktop** — [Getting Started with Local MCP Servers](https://support.claude.com/en/articles/10949351-getting-started-with-local-mcp-servers-on-claude-desktop)  (you'll probably want to dowload the `.mcpb` file from the latest release)
- **Claude Code** — [MCP guide](https://code.claude.com/docs/en/mcp)
- **OpenAI Codex** — [MCP guide](https://developers.openai.com/codex/mcp)
- **Grok Build** — appears to use the same configuration syntax as Claude Code

For manual configuration in claude code, put this in your `~/.claude.json` file, inside one of the configured projects. (Projects appear whenever you run `claude` in a directory.)

```json
{
  "mcpServers": {
    "j": {
      "command": "/path/to/j-mcp",
      "env": { "JHOME": "/path/to/j-install/bin" }
    }
  }
}
```

## Build

```
make
```

Produces `build/j-mcp` (or `build/j-mcp.exe` on Windows).

Supported toolchains: native gcc/clang on Linux and macOS, MinGW-w64 on
Windows (via MSYS2). CI builds all three on every push; tagged releases
attach the binaries and the `.mcpb` / `.dxt` bundle as release assets.

### Finding libj

The server searches for libj in this order:

1. `--libj <path>` command-line flag
2. `$JHOME/libj.so` — J's conventional install-root variable, set by most
   packaged distributions
3. A `libj.so` sibling of the `j-mcp` binary (jconsole's own discovery rule)
4. Bare `libj.so` via `LD_LIBRARY_PATH` and the system linker search path

## Transport

MCP over stdio with newline-delimited JSON-RPC framing (one message per
line, no embedded newlines). Logs go to stderr as JSONL. SIGINT is **not**
trapped — it terminates the server; breaking a running J sentence uses the
`j_break` tool or MCP's `$/cancelRequest`.

## Sandbox

Sessions created with `sandbox: true` immediately evaluate `(9!:25) 1`,
setting `JT(jt,seclev)`. This blocks shell escapes and restricts file loads to
`.ijs` / `.js`. The security level is monotonic; lowering it requires session
restart.

## License

See `LICENSE`.
