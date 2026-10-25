"""A fake control plane, for the client's tests.

The real one is `finnd`, on the device. A test needs the wire, not the
device, so this speaks the same line protocol over a unix socket in a
thread of this process: the same requests, the same replies, and the
same errors.

The protocol is line-oriented, so a reply is either a line of text or
a binary payload after a line that says how long it is.
"""

import os
import socket
import tempfile
import threading

# The frame the fake device hands back for a capture: a PPM header and
# a few bytes of pixels. The client does not look inside it.
PPM = b"P6\n2 2\n255\n" + bytes(range(12))


class FakeServer:
    """A unix socket that speaks the control protocol."""

    def __init__(self, handler=None):
        self.dir = tempfile.mkdtemp(prefix="finn-test-")
        self.path = os.path.join(self.dir, "control.sock")

        self._sock = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
        self._sock.bind(self.path)
        self._sock.listen(4)

        self.requests = []
        self.handler = handler if handler is not None else default_handler
        self.overrides = {}
        self.notifications = []
        self.settings_page = "wifi"

        self._thread = threading.Thread(target=self._serve, daemon=True)
        self._thread.start()

    # ---- the socket ----

    def _serve(self):
        while True:
            try:
                conn, _addr = self._sock.accept()
            except OSError:
                return
            threading.Thread(target=self._session, args=(conn,), daemon=True).start()

    def _session(self, conn):
        buf = b""
        with conn:
            while True:
                try:
                    chunk = conn.recv(4096)
                except OSError:
                    return
                if not chunk:
                    return
                buf += chunk
                while b"\n" in buf:
                    line, buf = buf.split(b"\n", 1)
                    self._answer(conn, line.decode("utf-8"))

    def _answer(self, conn, request):
        self.requests.append(request)
        reply = self.handler(request, self)
        if isinstance(reply, bytes):
            head = ("OK BINARY %d\n" % len(reply)).encode("ascii")
            conn.sendall(head + reply)
            return
        conn.sendall((reply + "\n").encode("utf-8"))

    def close(self):
        try:
            self._sock.close()
        except OSError:
            pass
        try:
            os.unlink(self.path)
        except OSError:
            pass

    def __enter__(self):
        return self

    def __exit__(self, *exc):
        self.close()
        return False


def default_handler(request, server):
    """A small device: the verbs the client's tests use."""
    verb = request.split(" ", 1)[0]
    rest = request[len(verb):].strip()

    if verb == "ping":
        return "OK FINNCTL/1"
    if verb == "version":
        return "OK finnd=0.1 proto=FINNCTL/1"

    if verb == "state.get":
        state = {
            "battery": 87, "charging": 0, "wifi": 3, "volume": 70,
            "brightness": 60, "bluetooth": -1, "wired": 1, "dark": 0,
        }
        state.update(server.overrides)
        return "OK " + " ".join("%s=%d" % kv for kv in sorted(state.items()))

    if verb == "state.set":
        for token in rest.split():
            key, _, value = token.partition("=")
            server.overrides[key] = int(value)
        return "OK"

    if verb == "state.clear":
        server.overrides.clear()
        return "OK"

    if verb == "screen.info":
        return "OK w=2560 h=1600 scale=1.00"
    if verb == "screen.pointer":
        return "OK x=0 y=0 buttons=0"

    if verb == "pointer.move" or verb == "pointer.by" or verb == "pointer.click":
        return "OK"

    if verb in ("key.press", "key.release", "key.tap"):
        return "OK"

    if verb == "screen.capture":
        return PPM

    if verb == "app.list":
        return ("OK count=3"
                " [notes|installed|Finn Notes]"
                " [code|available|Finn Code]"
                " [files|installed|Finn Files]")

    if verb == "app.launch":
        if rest == "notes" or rest == "files":
            return "OK launched %s" % rest
        return "ERR unknown app"

    if verb == "window.list":
        return ("OK count=2"
                " [1|notes|normal|Finn Notes]"
                " [2|code|maximized|Finn Code]")

    if verb == "window.open":
        return "OK id=3"

    if verb in ("window.close", "window.focus", "window.minimize",
                "window.maximize", "window.move"):
        return "OK"

    if verb == "setup.get":
        return "OK active=1 done=0 lang=en"

    if verb == "setup.show" or verb == "setup.complete":
        return "OK"

    if verb == "settings.page":
        name = "wifi"
        for part in rest.split():
            if part.startswith("name="):
                name = part.split("=", 1)[1]
            elif "=" not in part:
                name = part
        if name in ("wifi", "bluetooth", "display", "sound", "power", "about", "update"):
            server.settings_page = name
            return "OK"
        return "ERR no such page"

    if verb == "settings.get":
        page = getattr(server, "settings_page", "wifi")
        return "OK page=%s" % page

    if verb == "notify.list":
        return "OK count=1 [Hello|World|09:41]"

    if verb == "notify.post":
        server.notifications.append(rest)
        return "OK"

    if verb == "notify.clear":
        server.notifications = []
        return "OK"

    return "ERR unknown verb %s" % verb
