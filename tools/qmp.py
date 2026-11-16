#!/usr/bin/env python3
"""Small QMP client: screenshots, mouse clicks and keyboard input for QEMU tests."""
import json
import socket
import sys
import time


class QMP:
    def __init__(self, path):
        self.s = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
        for _ in range(100):
            try:
                self.s.connect(path)
                break
            except OSError:
                time.sleep(0.2)
        self.f = self.s.makefile("rw")
        self.f.readline()
        self.cmd("qmp_capabilities")

    def cmd(self, name, **args):
        msg = {"execute": name}
        if args:
            msg["arguments"] = args
        self.f.write(json.dumps(msg) + "\n")
        self.f.flush()
        while True:
            line = self.f.readline()
            if not line:
                raise RuntimeError("QMP closed")
            r = json.loads(line)
            if "return" in r or "error" in r:
                return r

    def shot(self, path):
        return self.cmd("screendump", filename=path)

    def move(self, x, y, w, h):
        ax = int(x * 32767 / (w - 1))
        ay = int(y * 32767 / (h - 1))
        return self.cmd("input-send-event", events=[
            {"type": "abs", "data": {"axis": "x", "value": ax}},
            {"type": "abs", "data": {"axis": "y", "value": ay}}])

    def click(self, x, y, w=1280, h=800):
        self.move(x, y, w, h)
        time.sleep(0.3)
        self.cmd("input-send-event", events=[{"type": "btn", "data": {"down": True, "button": "left"}}])
        time.sleep(0.25)
        self.cmd("input-send-event", events=[{"type": "btn", "data": {"down": False, "button": "left"}}])
        time.sleep(0.5)

    def key(self, *keys):
        """keys: qcode names pressed together (e.g. 'shift', 'a')."""
        down = [{"type": "key", "data": {"down": True, "key": {"type": "qcode", "data": k}}} for k in keys]
        up = [{"type": "key", "data": {"down": False, "key": {"type": "qcode", "data": k}}} for k in reversed(keys)]
        self.cmd("input-send-event", events=down)
        time.sleep(0.08)
        self.cmd("input-send-event", events=up)
        time.sleep(0.08)

    def type_us(self, text):
        """Types text using the US keyboard layout."""
        special = {" ": ["spc"], "-": ["minus"], ".": ["dot"], ",": ["comma"], "_": ["shift", "minus"],
                   "@": ["shift", "2"], "/": ["slash"]}
        for ch in text:
            if ch in special:
                self.key(*special[ch])
            elif ch.isdigit():
                self.key(ch)
            elif ch.isalpha():
                if ch.isupper():
                    self.key("shift", ch.lower())
                else:
                    self.key(ch)

    def type(self, text):
        """Types text using the German keyboard layout (QWERTZ)."""
        special = {" ": ["spc"], "-": ["slash"], ".": ["dot"], ",": ["comma"], "_": ["shift", "slash"],
                   "ä": ["apostrophe"], "ö": ["semicolon"], "ü": ["bracket_left"], "ß": ["minus"],
                   "Ä": ["shift", "apostrophe"], "Ö": ["shift", "semicolon"], "Ü": ["shift", "bracket_left"],
                   "@": ["altgr", "q"], "/": ["shift", "7"]}
        for ch in text:
            if ch in special:
                self.key(*special[ch])
            elif ch.isdigit():
                self.key(ch)
            elif ch.isalpha():
                k = ch.lower()
                if k == "y":
                    k = "z"
                elif k == "z":
                    k = "y"
                if ch.isupper():
                    self.key("shift", k)
                else:
                    self.key(k)


if __name__ == "__main__":
    q = QMP(sys.argv[1])
    print(q.cmd(sys.argv[2]))
