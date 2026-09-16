# j-mcp — MCP server for the J programming language

A lightweight [Model Context Protocol](https://modelcontextprotocol.io) server
that embeds the J interpreter via `libj.{so,dylib,dll}` and exposes it to LLM
clients.

Written in **C11** with no dependencies beyond libc, pthreads, and libdl.
Intended to be contributable upstream into
[jsource](https://github.com/jsoftware/jsource) once the surface stabilises.

## Demo video

[<img width="640" height="360" alt="image" src="https://github.com/user-attachments/assets/7fceda2b-fe5b-47d2-a45a-4e3d627ffcc3" />](https://www.youtube.com/watch?v=l0lJxHoII-M)


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

## Registering your own tool

Registration is active: call `j_tool_register`; loading a file or defining
metadata alone does not register a tool.

- Create the backing session with `j_session_create` first.
- `body` must be **one J sentence** that defines exactly `mcp_<name>_z_`
  as a monadic verb. For longer implementations, use `load '/absolute/path/tool.ijs'`.
  Helpers are fine; the entry-point name must match the registered tool name.
- The verb receives the **raw JSON string** in `y` and should return JSON text.
  In the loaded script, parse and encode with `convert/json`:

  ```j
  require 'convert/json'
  mcp_echo_z_=: 3 : 0
    obj=. dec_json_json_ y
    enc_json_json_ obj
  )
  ```

- `inputSchema` must be a JSON **object**, not a JSON-encoded string.
  Omission defaults to `{"type":"object"}`.
- `j_tool_register` uses `name` for the tool and `session` for its session.
  `j_eval` accepts `name` or the alias `session` for its session;
  `name` takes precedence when both are present.
- Registrations persist at `$XDG_STATE_HOME/j-mcp/tools.json` (default
  `~/.local/state/j-mcp/tools.json`; Windows: `%APPDATA%\j-mcp\tools.json`).
  Bodies replay on session (re)creation. After a server restart, recreate the
  session before calling its saved tools.

For a name mismatch, check `4!:0 <'mcp_echo_z_'` in the backing session:
`3` means verb. See the [complete tutorial](docs/tutorial-registering-a-tool.md)
for registration, listing, calls, replay and troubleshooting.

### Bundled lint tool

[tools/j_lint.ijs](tools/j_lint.ijs), contributed by **Tom McGuire
(@tmcguirefl)**, exposes `j_lint` for checking J files or inline source.
Only the adapter is vendored. Obtain its `report.ijs` dependency from
[tmcguirefl/jlinter](https://github.com/tmcguirefl/jlinter) and install
Henry Rich's `debug/lint` and J's `convert/json` addons separately.
Install jlinter at `~addons/tmcguire/jlinter`, or place the adapter beside
the external `report.ijs`.

Create session `jlinter`, then call `j_tool_register` with `name: "j_lint"`,
`session: "jlinter"`, and `body: "load '/absolute/path/to/j-mcp/tools/j_lint.ijs'"`.
Pass [Tom's inputSchema object from the tutorial](docs/tutorial-registering-a-tool.md#create--register--list--call);
do not pass the string noun `mcp_j_lint_schema_z_` without decoding it.
Then list tools and call `j_lint` with `path` or `source`, plus optional
`format` (`json`, `markdown`, or `text`).

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

### Minimum J version

**J 9.8 beta is required** (tested with **9.8.0-beta7**). Stock J through
9.03 does not export `JInterrupt`; the server hard-fails at startup with
`libj missing symbol: JInterrupt`. Use a full J installation with its profile
and addons. For example:

```sh
JHOME=/absolute/path/to/j9.8/bin build/j-mcp
# or:
build/j-mcp --libj /absolute/path/to/j9.8/bin/libj.so
```

### Finding libj

The server searches for libj in this order:

1. `--libj <path>` command-line flag
2. `$JHOME/libj.so` — J's conventional install-root variable, set by most
   packaged distributions
3. A `libj.so` sibling of the `j-mcp` binary (jconsole's own discovery rule)
4. Bare `libj.so` via `LD_LIBRARY_PATH` and the system linker search path

### Integration tests

Build first, then run the stdio regression suite with Python 3 and J 9.8 beta:

```sh
JHOME=/absolute/path/to/j9.8/bin \
JLINTER_DIR=/absolute/path/to/jlinter \
python3 -m unittest discover -s tests -v
```

`JLINTER_DIR` points to an external jlinter checkout with `report.ijs` and
fixtures. Without it, the lint test is skipped. Tests use temporary registry
directories and do not change your saved tools.

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
