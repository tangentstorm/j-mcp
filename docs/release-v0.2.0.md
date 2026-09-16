# v0.2.0

Tool registration now rejects non-object `inputSchema` values before evaluating
the body, preventing invalid MCP tool listings. Saved entries with invalid
schemas are skipped with a diagnostic.

- Bundle Tom McGuire's (@tmcguirefl) `j_lint` adapter in source and MCPB/DXT
  packages. The external jlinter `report.ijs`, Henry Rich's `debug/lint`, and
  `convert/json` remain separate dependencies.
- Explain active registration, single-sentence bodies, the exact
  `mcp_<name>_z_` monadic entry point, raw JSON arguments, and replay.
- Add an end-to-end lint registration tutorial and actionable name-mismatch errors.
- Accept `session` as an alias for `j_eval`'s existing `name` parameter.
- Document the J 9.8 beta floor (tested with 9.8.0-beta7); stock J through 9.03
  lacks the required `JInterrupt` symbol.
- Add stdio integration tests for registration, validation, calls, persistence,
  replay, unregister, and the bundled lint adapter.

Linux validation: clean GCC build and all six integration tests passed using
J 9.8.0-beta7 and jlinter b9b2d25953d5f7912ede1abb7279ca9d4ba01de9.
See [test results](test-results-v0.2.0.md) for coverage and known output behavior.
