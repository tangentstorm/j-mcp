NB. MCP adapter for jlinter (j-mcp j_tool_register binding)
NB. Contributed by Tom McGuire (@tmcguirefl).
NB. Copyright (c) 2026 jlinter contributors. MIT; see LICENSE.jlinter.
NB. Depends on https://github.com/tmcguirefl/jlinter report.ijs and
NB. Henry Rich's debug/lint addon; only this adapter is vendored here.
NB. See docs/tutorial-registering-a-tool.md for bundled registration.
NB. Defines monadic verb mcp_j_lint_z_  (JSON string → JSON string).
NB.
NB. Args JSON:
NB.   {"path":"/file.ijs"}
NB.   {"source":"…j code…"}
NB.   optional "format": "json" (default) | "markdown" | "text"
NB.
NB. Result:
NB.   {"ok":bool,"exit_code":0|1,"format":"…","path":"…","report":…}
NB.
NB. Bundled copy: load '/absolute/path/to/j-mcp/tools/j_lint.ijs'
NB. Upstream addon installation:
NB.   load 'tmcguire/jlinter/mcp_j_lint'
NB.   or load jpath '~addons/tmcguire/jlinter/mcp_j_lint.ijs'
NB.
NB. j-mcp registration metadata (schema is NOT auto-scraped by j-mcp;
NB. agents must pass inputSchema to j_tool_register). After load:
NB.   mcp_j_lint_schema_z_        JSON Schema string for inputSchema
NB.   mcp_j_lint_description_z_   human description
NB.   mcp_j_lint_toolspec_z_ ''   JSON object {name,description,inputSchema}
NB. Register with body: load 'tmcguire/jlinter/mcp_j_lint'

mcp_j_lint_adapter_path_z_=: > {: 4!:3 ''

NB. Locate report.ijs (prefer installed shortname path; never trust {:4!:3 after
NB. earlier loads which may point at fixtures or unrelated scripts).
(3 : 0) ''
  cands=. jpath each 'tmcguire/jlinter/report.ijs';'~addons/tmcguire/jlinter/report.ijs'
  me=. > {: 4!:3 ''
  slash=. me i: '/'
  cands=. cands , < ((slash + 1) {. me) , 'report.ijs'
  found=. 0
  for_c. cands do.
    p=. > c
    if. 1:@(1!:4) :: 0: < p do.
      load p
      found=. 1
      break.
    end.
  end.
  if. -. found do.
    smoutput 'mcp_j_lint: could not find report.ijs among:'
    smoutput ; LF , each cands
  end.
)

require 'convert/json'

NB. ------------------------------------------------------------------
NB. j-mcp tool metadata (consumed by j_tool_register as inputSchema /
NB. description — j-mcp stores these as MCP tool metadata, not by
NB. inspecting J nouns automatically).
NB. ------------------------------------------------------------------

mcp_j_lint_description_z_=: 'Static-check a J script or snippet via debug/lint. Returns {ok,exit_code,format,path,report}. Pass path to a .ijs file or inline source; optional format is json (default), markdown, or text. Use after editing .ijs to drive a fix loop.'

NB. Single-line JSON schema (no embedded LF) so it is safe to embed as
NB. inputSchema after decoding to an object; never pass this noun as a string.
mcp_j_lint_schema_z_=: '{"type":"object","properties":{"path":{"type":"string","description":"Path to a .ijs file to lint"},"source":{"type":"string","description":"Inline J script text (alternative to path)"},"format":{"type":"string","enum":["json","markdown","text"],"description":"Report format; default json","default":"json"}},"additionalProperties":false}'

NB. Registration toolspec: name + description + inputSchema as one JSON
NB. object string (session/body still required by j_tool_register).
mcp_j_lint_toolspec_z_=: 3 : 0
'{"name":"j_lint","description":' , (enc_json_json_ mcp_j_lint_description_z_) , ',"inputSchema":' , mcp_j_lint_schema_z_ , '}'
)

NB. Full j_tool_register args JSON. Optional y=session name (default jlinter).
NB. Emit this noun, parse as object, pass each field to j_tool_register
NB. (especially inputSchema as an object, not a string).
mcp_j_lint_register_args_z_=: 3 : 0
sess=. y
if. 0 = # sess do. sess=. 'jlinter' end.
if. 0 = L. sess do. sess=. , sess else. sess=. > sess end.
body=. 'load ' , quote mcp_j_lint_adapter_path_z_
'{"name":"j_lint","session":' , (enc_json_json_ sess) , ',"description":' , (enc_json_json_ mcp_j_lint_description_z_) , ',"inputSchema":' , mcp_j_lint_schema_z_ , ',"body":' , (enc_json_json_ body) , '}'
)

NB. x=field name, y=2-row object from convert/json
jlint_get_z_=: 4 : 0
if. 0 = # y do. '' return. end.
i=. (0 { y) i. < x
if. i >: {: $ y do. '' return. end.
> (< 1 , i) { y
)

NB. j-mcp requires mcp_<toolname>_z_ as a monadic verb
mcp_j_lint_z_=: 3 : 0
raw=. y
if. 0 = L. raw do. raw=. , raw else. raw=. > raw end.
if. 0 = # raw do. raw=. '{}' end.

obj=. dec_json_json_ :: ((0 0 $ a:)"_) raw
if. 0 = # obj do.
  '{"ok":false,"exit_code":1,"format":"json","path":"","report":"invalid JSON args"}' return.
end.

path=. 'path' jlint_get_z_ obj
src=. 'source' jlint_get_z_ obj
fmt=. 'format' jlint_get_z_ obj
if. 0 = # fmt do. fmt=. 'json' end.
if. -. (< fmt) e. 'json';'markdown';'text' do. fmt=. 'json' end.

if. 0 < # src do.
  target=. 'source=' , src
elseif. 0 < # path do.
  target=. path
elseif. do.
  ('{"ok":false,"exit_code":1,"format":' , (enc_json_json_ fmt) , ',"path":"","report":"missing path or source"}') return.
end.

rep=. report_jlinter_ fmt ; target
ec=. 0 >. exitcode_jlinter_ ''
okflag=. (0 = ec) {:: 'false' ; 'true'
pathout=. path
if. 0 = # pathout do. pathout=. '<source>' end.

if. fmt -: 'json' do.
  '{"ok":' , okflag , ',"exit_code":' , (": ec) , ',"format":"json","path":' , (enc_json_json_ pathout) , ',"report":' , rep , '}'
else.
  '{"ok":' , okflag , ',"exit_code":' , (": ec) , ',"format":' , (enc_json_json_ fmt) , ',"path":' , (enc_json_json_ pathout) , ',"report":' , (enc_json_json_ rep) , '}'
end.
)
