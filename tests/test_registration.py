"""Integration tests adapted from the ticket #7 stdio evaluation scenarios.

JHOME=/path/to/j9.8/bin JLINTER_DIR=/path/to/jlinter python3 -m unittest discover -s tests -v
JLINTER_DIR is optional; lint integration is skipped unless supplied.
"""
import json
import os
from pathlib import Path
import shutil
import tempfile
import unittest

from mcp_client import Client

ROOT = Path(__file__).resolve().parents[1]


class RegistrationTests(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.addCleanup(self.tmp.cleanup)
        self.state = str(Path(self.tmp.name) / "state")
        self.start()
        self.ok("j_session_create", {"name": "s1"})

    def start(self):
        self.c = Client(self.state)
        self.addCleanup(self.c.close)
        self.c.req("initialize", {"protocolVersion": "2025-06-18"})

    def ok(self, name, args):
        reply = self.c.call(name, args)
        self.assertNotIn("error", reply)
        result = reply["result"]
        self.assertFalse(result.get("isError"), reply)
        return result.get("structuredContent", result)

    def error(self, name, args, expected):
        reply = self.c.call(name, args)
        self.assertTrue(reply["result"].get("isError"), reply)
        self.assertIn(expected, reply["result"]["content"][0]["text"])

    def register(self, name="echo", **kwargs):
        args = dict(name=name, session="s1", body=f"mcp_{name}_z_=: 3 : 'y'",
                    inputSchema={"type": "object"})
        args.update(kwargs)
        return args

    def listed(self):
        return {t["name"]: t for t in self.c.req("tools/list")["result"]["tools"]}

    def saved(self):
        return json.loads((Path(self.state) / "j-mcp/tools.json").read_text())["tools"]

    def test_lifecycle(self):
        self.ok("j_tool_register", self.register())
        self.assertEqual(self.ok("echo", {"x": 21}), {"x": 21})
        self.assertIsInstance(self.listed()["echo"]["inputSchema"], dict)
        self.assertEqual(self.saved()[0]["inputSchema"], {"type": "object"})
        self.assertEqual(self.ok("j_tool_list", {})["tools"][0]["name"], "echo")
        self.assertIn("notifications/tools/list_changed",
                      [n["method"] for n in self.c.notifications])
        self.ok("j_session_restart", {"name": "s1"})
        self.assertEqual(self.ok("echo", {"x": 22}), {"x": 22})
        self.c.close()
        self.start()
        self.assertIn("echo", self.listed())
        self.error("echo", {}, "backing session s1 is not live")
        self.ok("j_session_create", {"name": "s1"})
        self.assertEqual(self.ok("echo", {"x": 23}), {"x": 23})
        self.ok("j_tool_unregister", {"name": "echo"})
        self.assertNotIn("echo", self.listed())
        self.assertEqual(self.saved(), [])
        self.c.close()
        self.start()
        self.assertNotIn("echo", self.listed())

    def test_schema_rejection_before_body_execution(self):
        for schema in ['{"type":"object"}', [], 1, 1.5, True, None]:
            with self.subTest(schema=schema):
                self.error("j_tool_register", self.register(inputSchema=schema),
                           "inputSchema must be a JSON object")
                result = self.ok("j_eval", {"name": "s1", "sentence": "4!:0 <'mcp_echo_z_'"})
                self.assertEqual(result["stdout"].strip(), "_1")
                self.assertNotIn("echo", self.listed())
        self.assertFalse((Path(self.state) / "j-mcp/tools.json").exists())
        args = self.register()
        del args["inputSchema"]
        self.ok("j_tool_register", args)
        self.assertEqual(self.listed()["echo"]["inputSchema"], {"type": "object"})

    def test_failure_messages(self):
        self.error("j_tool_register", self.register(session="missing"), "no session missing")
        for body in ["other_z_=: 3 : 'y'", "mcp_echo_z_=: 42"]:
            self.error("j_tool_register", self.register(body=body),
                       "body did not define mcp_echo_z_ as a verb; body must define exactly "
                       "mcp_<name>_z_ as a monadic verb")
        self.error("j_tool_register", self.register(body="other_z_=: 0"),
                   "4!:0 <'mcp_echo_z_' (expected 3)")
        self.error("j_tool_register", self.register(body="load '/nonexistent/tool.ijs'"), "body ec=")
        self.error("j_tool_register", self.register(body=""), "missing body")
        self.error("j_tool_register", self.register(name="1bad"), "invalid name")
        self.error("j_tool_register", self.register(body="require 'convert/json'\nmcp_echo_z_=: ]"),
                   "body ec=")
        self.ok("j_tool_register", self.register())
        self.error("j_tool_register", self.register(), "tool already registered")
        self.error("j_tool_unregister", {"name": "unknown"}, "no such registered tool")

    def test_eval_alias_and_json_parsing(self):
        for selector in [{"name": "s1"}, {"session": "s1"},
                         {"name": "s1", "session": "missing"}]:
            result = self.ok("j_eval", dict(selector, sentence="1+1"))
            self.assertEqual(result["ec"], 0)
            self.assertEqual(result["stdout"].strip(), "2")
        schema = self.listed()["j_eval"]["inputSchema"]
        self.assertIn("session", schema["properties"])
        self.assertEqual(schema["anyOf"], [{"required": ["name"]}, {"required": ["session"]}])
        script = Path(self.tmp.name) / "echo.ijs"
        script.write_text("require 'convert/json'\nmcp_echo_z_=: 3 : 0\n"
                          "obj=. dec_json_json_ y\nenc_json_json_ obj\n)\n")
        self.ok("j_tool_register", self.register(body=f"load '{script}'"))
        self.assertEqual(self.ok("echo", {"x": 21}), {"x": 21})

    def test_legacy_invalid_schema_is_not_published(self):
        self.ok("j_tool_register", self.register())
        self.c.close()
        path = Path(self.state) / "j-mcp/tools.json"
        data = json.loads(path.read_text())
        bad = dict(data["tools"][0], name="legacy", inputSchema='{"type":"object"}')
        data["tools"].append(bad)
        path.write_text(json.dumps(data))
        self.start()
        self.assertIn("echo", self.listed())
        self.assertNotIn("legacy", self.listed())

    @unittest.skipUnless(os.environ.get("JLINTER_DIR"), "set JLINTER_DIR for lint integration")
    def test_vendored_lint(self):
        engine = Path(os.environ["JLINTER_DIR"])
        adapter = Path(self.tmp.name) / "j_lint.ijs"
        shutil.copyfile(ROOT / "tools/j_lint.ijs", adapter)
        shutil.copyfile(engine / "report.ijs", adapter.with_name("report.ijs"))
        output_setup = self.ok("j_eval", {"session": "s1", "sentence": "9!:37[0 100000 100000 100000"})
        self.assertEqual(output_setup["ec"], 0)
        schema = json.loads((ROOT / "tools/j_lint.ijs").read_text().split(
            "mcp_j_lint_schema_z_=: '")[1].split("'")[0])
        self.ok("j_tool_register", self.register(name="j_lint", body=f"load '{adapter}'",
                                                inputSchema=schema))
        self.assertEqual(self.listed()["j_lint"]["inputSchema"], schema)
        check = self.ok("j_eval", {"session": "s1", "sentence": "4!:0 <'mcp_j_lint_z_'"})
        self.assertEqual(check["stdout"].strip(), "3")
        helper = self.ok("j_eval", {"session": "s1",
                                   "sentence": "helper_out=. (1!:2&2) mcp_j_lint_register_args_z_ 's1'"})
        self.assertEqual(json.loads(helper["stdout"])["body"], f"load '{adapter}'")
        for args, expected in [
            ({"path": str(engine / "fixtures/bad_undefined.ijs")}, "Undefined name"),
            ({"source": "bad =: 3 : 0\n  undefined_name_here y\n)"}, "undefined_name_here"),
            ({"path": str(engine / "fixtures/good_mean.ijs"), "format": "markdown"}, "No errors found"),
            ({"path": str(engine / "fixtures/bad_control.ijs"), "format": "text"}, "control"),
        ]:
            with self.subTest(args=args):
                result = self.ok("j_lint", args)
                self.assertIn(expected, json.dumps(result))
                if "content" in result:
                    report = json.loads(result["content"][0]["text"].splitlines()[-1])
                else:
                    report = result
                clean = args.get("format") == "markdown"
                self.assertEqual(report["ok"], clean)
                self.assertEqual(report["exit_code"], 0 if clean else 1)
        self.c.close()
        self.start()
        self.assertIn("j_lint", self.listed())
        self.error("j_lint", {}, "backing session s1 is not live")
        self.ok("j_session_create", {"name": "s1"})
        self.ok("j_eval", {"session": "s1", "sentence": "9!:37[0 100000 100000 100000"})
        self.assertIn("undefined_name_here", json.dumps(self.ok("j_lint", {
            "source": "bad =: 3 : 0\n  undefined_name_here y\n)"})))
        self.ok("j_tool_unregister", {"name": "j_lint"})
        self.assertNotIn("j_lint", self.listed())


if __name__ == "__main__":
    unittest.main()
