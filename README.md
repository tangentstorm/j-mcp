# j-mcp — MCP server for the J programming language

A lightweight [Model Context Protocol](https://modelcontextprotocol.io) server
that embeds the J interpreter via `libj.{so,dylib,dll}` and exposes it to LLM
clients.

Written in **C11** with no dependencies beyond libc, pthreads, and libdl.
Intended to be contributable upstream into
[jsource](https://github.com/jsoftware/jsource) once the surface stabilises.

## Status

Phase 1 (MVP). See `PLAN.md` (internal) for the roadmap.

## Tools (phase 1)

| Tool | Purpose |
|---|---|
| `j.session.create` | Spawn a named J session (`sandbox: bool`). |
| `j.session.list` | Enumerate live sessions. |
| `j.session.terminate` | Destroy a named session. |
| `j.session.restart` | Terminate + recreate with same options. |
| `j.eval` | Evaluate a J sentence; structured result + captured output. |
| `j.parse` | Tokenize a sentence via `;:` and return the word list. |
| `j.break` | Interrupt a running sentence (non-blocking; uses `JInterrupt`). |
| `j.get` / `j.set` | Exchange dense numeric/char arrays via `JGetM`/`JSetM`. |
| `j.tool.register` | Register a new MCP tool whose body is a J verb. |
| `j.tool.unregister` | Drop a registered tool. |
| `j.tool.list` | Show registered J-defined tools. |

### Registered J-defined tools

An LLM (or human) can register new MCP tools whose implementation is a J verb
bound to a particular session. The body is persisted to
`$XDG_STATE_HOME/j-mcp/tools.json` and replayed into the session when it is
(re)created.

## Build

```
make
```

Produces `build/j-mcp` (or `build/j-mcp.exe` on Windows).

Supported toolchains: native gcc/clang on Linux and macOS, MinGW-w64 on
Windows (via MSYS2). CI builds all three on every push; tagged releases
attach the binaries as release assets.

### Finding libj

The server searches for libj in this order:

1. `--libj <path>` command-line flag
2. `$JHOME/libj.so` — J's conventional install-root variable, set by most
   packaged distributions
3. A `libj.so` sibling of the `j-mcp` binary (jconsole's own discovery rule)
4. Bare `libj.so` via `LD_LIBRARY_PATH` and the system linker search path

## Transport

MCP over stdio (LSP-style `Content-Length` framing). Logs go to stderr as
JSONL. SIGINT is **not** trapped — it terminates the server; breaking a running
J sentence uses the `j.break` tool or MCP's `$/cancelRequest`.

## Sandbox

Sessions created with `sandbox: true` immediately evaluate `(9!:25) 1`,
setting `JT(jt,seclev)`. This blocks shell escapes and restricts file loads to
`.ijs` / `.js`. The security level is monotonic; lowering it requires session
restart.

## License

See `LICENSE`.
