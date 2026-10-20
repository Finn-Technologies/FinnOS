"""
The control protocol, as the SDK sees it.

The device speaks a small line protocol, deliberately not JSON: the JSON
belongs at the MCP boundary, on the host, where a fast-moving external
spec can be kept out of the operating system.

This module is the one place in the SDK that knows the wire format. The
client above it deals in verbs and values; the transport below it deals in
bytes.
"""

import os
import socket

# The protocol's own name and version. The device answers `ping` with it.
PROTOCOL_MAGIC = "FINNCTL/1"

# The sockets. The control plane is the one to talk to; the compositor's
# socket is the provider underneath it, and is only reached when the
# control plane is not running.
CONTROL_SOCKET = "/run/finn/control.sock"
COMP_SOCKET = "/run/finn/comp.sock"

# Where the device's data lives, for the parts of the SDK that read it.
APPS_CATALOGUE = "/usr/share/finn/apps.registry"
APPS_INSTALLED = "/var/lib/finnpkg/installed"

# The line the protocol allows. A request longer than this is a bug
# somewhere, and the device would refuse it anyway.
MAX_LINE = 4096


class ProtocolError(Exception):
    """The other end said something the protocol does not allow."""


class ControlSocket:
    """
    One request, one reply, over a unix socket.

    The device's control plane keeps a connection open and serves many
    requests on it, so this class holds the connection and can be used as
    a context manager:

        with ControlSocket() as ctl:
            ctl.call("state.get")

    The transport is deliberately thin: it opens, writes one line, reads
    one reply, and hands back the bytes. Everything about what a reply
    *means* belongs to the client.
    """

    def __init__(self, path=CONTROL_SOCKET, timeout=10.0):
        self.path = path
        self.timeout = timeout
        self._sock = None

    def connect(self):
        if self._sock is not None:
            return self._sock
        sock = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
        sock.settimeout(self.timeout)
        try:
            sock.connect(self.path)
        except OSError:
            sock.close()
            raise
        self._sock = sock
        return sock

    def send(self, line):
        """Write one request line. The newline is added here."""
        if not line.endswith("\n"):
            line += "\n"
        data = line.encode("utf-8")
        if len(data) > MAX_LINE:
            raise ProtocolError("request longer than %d bytes" % MAX_LINE)
        self.connect().sendall(data)

    def read_line(self):
        """Read one reply line, without its newline."""
        buf = bytearray()
        sock = self.connect()
        while True:
            chunk = sock.recv(1)
            if not chunk:
                break
            if chunk == b"\n":
                break
            buf += chunk
            if len(buf) > MAX_LINE:
                raise ProtocolError("reply longer than %d bytes" % MAX_LINE)
        return buf.decode("utf-8", "replace")

    def read_exactly(self, count):
        """Read a binary payload of exactly `count` bytes."""
        sock = self.connect()
        out = bytearray()
        while len(out) < count:
            chunk = sock.recv(min(65536, count - len(out)))
            if not chunk:
                break
            out += chunk
        return bytes(out)

    def close(self):
        if self._sock is not None:
            try:
                self._sock.close()
            finally:
                self._sock = None

    def __enter__(self):
        self.connect()
        return self

    def __exit__(self, *exc):
        self.close()
        return False


def default_socket():
    """The socket to use, honouring FINN_CONTROL_SOCK for the tests."""
    return os.environ.get("FINN_CONTROL_SOCK", CONTROL_SOCKET)
