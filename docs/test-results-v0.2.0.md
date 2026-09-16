# v0.2.0 validation

Validated on Linux with J 9.8.0-beta7 and the external jlinter checkout
`b9b2d25953d5f7912ede1abb7279ca9d4ba01de9`. Neither jlinter's report engine nor
Henry Rich's lint implementation is vendored.

```sh
make clean && make
JHOME=/path/to/j9.8/bin JLINTER_DIR=/path/to/jlinter \
python3 -m unittest discover -s tests -v
git diff --check
```

Clean GCC build: passed, no warnings. Whitespace check: passed.
Integration suite: **6 tests passed, 0 skipped**.

| Test | Verified |
|---|---|
| Lifecycle | Create → register → JSON call → list → object schema on disk → list-change notification → session restart/replay → server restart → missing-session error → recreation/replay → unregister → absence after another restart |
| Schema validation | Reject string, array, integer, float, boolean and null before body execution; no listing or persistence side effects; omitted schema defaults to object |
| Registration failures | Missing session, wrong verb name, noun instead of verb, missing file, empty body, invalid name, multiline body, duplicate registration, unknown unregister; naming rule and self-check in error |
| Alias and JSON | Existing `j_eval.name`, new `session` alias, precedence when both supplied, advertised alias schema, tutorial's `convert/json` echo implementation |
| Legacy persistence | Skip an invalid saved string schema while retaining a valid saved tool |
| Vendored lint | Load adapter beside external `report.ijs`; exact schema and verb check; helper emits actual adapter path; bad file and inline undefined name; clean markdown and control-error text; report ok/exit_code; server restart/replay; unregister |

The earlier evaluation's four scripts were also rerun from the writable
`eval-copy` directory against this build and the bundled adapter. All exited
normally; their printed responses were reviewed. They are characterization
scripts, not assertion-based tests:

- Scenario 1 reproduced persistence/replay/unregister, and the known domain error
  from applying `2&*` directly to raw JSON text. The committed lifecycle test
  uses a valid JSON echo verb and asserts successful calls.
- Scenario 2 confirmed the new string-schema rejection and actionable naming
  errors. Its “missing inputSchema” case actually supplies a schema through
  its base dictionary; the committed suite tests genuine omission.
- Scenario 3 exercised all four lint calls and restart/replay, including the new
  `j_eval.session` alias.
- Scenario 4 returned JSON scalar `42` in both text content and structuredContent.

During test development, the default J display limit truncated the adapter's
long registration-helper JSON. Raising `9!:37` in the test session resolved it;
the tutorial documents this setting and that it must be reapplied after session
recreation. Tests assert complete parsed reports, not merely successful processes.
The lint engine can also emit prose before JSON, including “No errors found”;
those results arrive as text rather than structuredContent. This existing
behavior is documented rather than changed.
