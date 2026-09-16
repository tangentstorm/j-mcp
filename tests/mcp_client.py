#!/usr/bin/env python3
"""Minimal MCP client for j-mcp testing: newline-delimited JSON-RPC over stdio.
Adapted from the ticket #7 evaluation client, with bounded response waits."""
import json, os, subprocess, time
from pathlib import Path
from queue import Queue, Empty
from threading import Thread

BIN = str(Path(__file__).resolve().parents[1] / "build" / "j-mcp")

class Client:
    def __init__(self, state_dir):
        env = dict(os.environ)
        if "JHOME" not in env:
            raise RuntimeError("Set JHOME to the J 9.8 beta bin directory")
        env["XDG_STATE_HOME"] = state_dir
        os.makedirs(state_dir, exist_ok=True)
        self.p = subprocess.Popen([BIN], stdin=subprocess.PIPE,
                                  stdout=subprocess.PIPE, stderr=subprocess.DEVNULL,
                                  text=True, bufsize=1, env=env)
        self.lines = Queue()
        def read():
            for line in self.p.stdout:
                self.lines.put(line)
            self.lines.put(None)
        self.reader = Thread(target=read, daemon=True)
        self.reader.start()
        self._id = 0
        self.notifications = []

    def req(self, method, params=None, timeout=45):
        self._id += 1
        msg = {"jsonrpc": "2.0", "id": self._id, "method": method}
        if params is not None:
            msg["params"] = params
        self.p.stdin.write(json.dumps(msg) + "\n")
        self.p.stdin.flush()
        deadline = time.time() + timeout
        while time.time() < deadline:
            try:
                line = self.lines.get(timeout=max(0, deadline - time.time()))
            except Empty:
                raise TimeoutError(method)
            if not line:
                raise RuntimeError("server closed stdout")
            m = json.loads(line)
            if m.get("id") == self._id:
                return m
            if "method" in m:
                self.notifications.append(m)
        raise TimeoutError(method)

    def call(self, name, arguments=None):
        p = {"name": name}
        if arguments is not None:
            p["arguments"] = arguments
        return self.req("tools/call", p)

    def close(self):
        try: self.p.stdin.close()
        except Exception: pass
        try: self.p.terminate(); self.p.wait(timeout=5)
        except Exception:
            try: self.p.kill(); self.p.wait(timeout=5)
            except Exception: pass
        self.reader.join(timeout=5)
        self.p.stdout.close()
