# Registering a J tool with j-mcp

Registration is active: **call `j_tool_register`**. Loading a script or defining
metadata nouns alone does not publish an MCP tool. The server runs the supplied
body, checks the expected verb, publishes the tool, and saves the registration.

Use J 9.8 beta or newer (tested with 9.8.0-beta7), including its standard profile
and addons. Stock J through 9.03 lacks `JInterrupt`; j-mcp exits at startup with
`libj missing symbol: JInterrupt`. Point `JHOME` at the installation's `bin`
directory, or start `build/j-mcp --libj /absolute/path/to/j9.8/bin/libj.so`.

## Prepare the bundled lint adapter

[tools/j_lint.ijs](../tools/j_lint.ijs) is Tom McGuire's (@tmcguirefl) MCP
adapter. Only the adapter is bundled. Obtain `report.ijs` from
[tmcguirefl/jlinter](https://github.com/tmcguirefl/jlinter); it wraps Henry Rich's
`debug/lint` addon. The J installation also needs `convert/json`.

For an installation you can write to, use J Package Manager (each line is a
separate J sentence):

```j
load 'pacman'
install 'debug/lint'
install 'convert/json'
install 'github:tmcguirefl/jlinter'
```

Alternatively, clone jlinter and put a copy of this bundled adapter beside its
`report.ijs`. This needs no write access to J's addons directory when
`debug/lint` and `convert/json` are already installed:

```sh
git clone https://github.com/tmcguirefl/jlinter /absolute/path/to/jlinter
cp /absolute/path/to/j-mcp/tools/j_lint.ijs /absolute/path/to/jlinter/j_lint.ijs
```

The adapter searches the installed `tmcguire/jlinter/report.ijs` locations,
then beside itself. Keep these files at stable absolute paths for replay.

## Create → register → list → call

The examples below are complete JSON-RPC requests over the server's stdio
transport. Send each object on one line; formatting here is for readability.
An MCP client normally handles initialization and the request envelopes.

Initialize the connection:

```json
{"jsonrpc":"2.0","id":1,"method":"initialize","params":{"protocolVersion":"2025-06-18","capabilities":{},"clientInfo":{"name":"tutorial","version":"1"}}}
{"jsonrpc":"2.0","method":"notifications/initialized"}
```

Create the backing session first:

```json
{"jsonrpc":"2.0","id":2,"method":"tools/call","params":{"name":"j_session_create","arguments":{"name":"jlinter","sandbox":false}}}
```

J's default display limit can truncate long JSON output. Increase it in this
session before calling the adapter (repeat after session recreation):

```json
{"jsonrpc":"2.0","id":20,"method":"tools/call","params":{"name":"j_eval","arguments":{"session":"jlinter","sentence":"9!:37[0 100000 100000 100000"}}}
```

Choose the body `load '/absolute/path/to/j-mcp/tools/j_lint.ijs'` when jlinter
is installed as an addon. For the adjacent-file setup above, use
`load '/absolute/path/to/jlinter/j_lint.ijs'` instead. The body is **one J
sentence**, even though the loaded file contains many lines.

Call `j_tool_register` with the adapter's input schema as an object:

```json
{
  "jsonrpc": "2.0",
  "id": 3,
  "method": "tools/call",
  "params": {
    "name": "j_tool_register",
    "arguments": {
      "name": "j_lint",
      "session": "jlinter",
      "description": "Static-check a J script or snippet via debug/lint.",
      "body": "load '/absolute/path/to/j-mcp/tools/j_lint.ijs'",
      "inputSchema": {
        "type": "object",
        "properties": {
          "path": {"type": "string", "description": "Path to a .ijs file to lint"},
          "source": {"type": "string", "description": "Inline J script text (alternative to path)"},
          "format": {
            "type": "string",
            "enum": ["json", "markdown", "text"],
            "description": "Report format; default json",
            "default": "json"
          }
        },
        "additionalProperties": false
      }
    }
  }
}
```

A successful response contains `ok: true`. Inspect the published tool and saved
registration, then call it:

```json
{"jsonrpc":"2.0","id":4,"method":"tools/list"}
{"jsonrpc":"2.0","id":5,"method":"tools/call","params":{"name":"j_tool_list","arguments":{}}}
{"jsonrpc":"2.0","id":6,"method":"tools/call","params":{"name":"j_lint","arguments":{"source":"bad =: 3 : 0\n  undefined_name_here y\n)","format":"json"}}}
```

Expect a lint report identifying `undefined_name_here`, with `ok: false` and
`exit_code: 1`. This is a successful tool call reporting a problem in the
submitted J code. You can instead supply `{"path":"/absolute/path/to/file.ijs"}`.

The lint engine sometimes prints prose before the returned JSON, particularly
in markdown/text modes and on clean files. Those calls may return text content
instead of `structuredContent`; clients should handle both.

## The number-one trap: name versus verb

For tool name `j_lint`, the body must define exactly `mcp_j_lint_z_` as a
monadic verb. Naming the tool `lint` asks for `mcp_lint_z_` instead and yields:

```text
body did not define mcp_lint_z_ as a verb; body must define exactly mcp_<name>_z_ as a monadic verb. Check with 4!:0 <'mcp_lint_z_' (expected 3)
```

Self-check the actual name with `j_eval` in the backing session:

```json
{"jsonrpc":"2.0","id":7,"method":"tools/call","params":{"name":"j_eval","arguments":{"session":"jlinter","sentence":"4!:0 <'mcp_j_lint_z_'"}}}
```

The result should have `ec: 0` and stdout `3` (verb). For any name `X`, the
check is `4!:0 <'mcp_X_z_'`. This checks the part of speech; implement monadic
behavior because calls supply only `y`.

`j_eval` accepts either `name` or `session` for its session, with `name`
taking precedence if both are supplied. In `j_tool_register`, `name` is the
tool name and `session` is the backing session.

## Raw JSON and schemas

The verb receives a **raw JSON string**, not a J number or an already-decoded
object. For a small example, save this as `/absolute/path/to/echo.ijs`:

```j
require 'convert/json'
mcp_echo_z_=: 3 : 0
  obj=. dec_json_json_ y
  enc_json_json_ obj
)
```

Register it with `name: "echo"`, `session: "jlinter"`,
`body: "load '/absolute/path/to/echo.ijs'"`, and
`inputSchema: {"type":"object"}`. A call with `{"x":21}` returns that object.
Load dependencies in the script so replay restores them too.

`inputSchema` must be a JSON object when supplied; omission defaults to
`{"type":"object"}`. Strings, arrays, numbers, booleans and null are rejected
with `inputSchema must be a JSON object`.

Tom's `mcp_j_lint_schema_z_` noun is a JSON **string**. Decode it in your client
before passing it as `inputSchema`. The helper
`mcp_j_lint_register_args_z_ 'jlinter'` produces a full JSON argument string
using the loaded adapter's path; parse that string as an object and explicitly
call `j_tool_register`. For unwrapped output through `j_eval`, use
`out=. (1!:2&2) mcp_j_lint_register_args_z_ 'jlinter'`.
The helper does not register anything itself.

## Persistence and cleanup

On Unix, registrations live at `$XDG_STATE_HOME/j-mcp/tools.json`, defaulting
to `~/.local/state/j-mcp/tools.json`. On Windows they live at
`%APPDATA%\j-mcp\tools.json`. Bodies replay on session (re)creation.

After restarting the server, the tool is listed immediately, but calling it
before recreating `jlinter` returns `backing session jlinter is not live`.
Call `j_session_create` again to replay the saved body; you do not need to
register again. `j_session_restart` also replays the body.

To remove the registration:

```json
{"jsonrpc":"2.0","id":8,"method":"tools/call","params":{"name":"j_tool_unregister","arguments":{"name":"j_lint"}}}
```

Register/unregister emits `notifications/tools/list_changed`. If your client
caches its tool list, refresh or reconnect. To replace a registration, unregister
first, then register the new body. Older saved entries with non-object schemas
are skipped with a stderr diagnostic; register them again with an object.
