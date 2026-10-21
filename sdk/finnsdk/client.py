"""
The control-plane client.

This is the SDK's front door: it turns the protocol's verbs into calls a
program wants to make, and the protocol's key=value payloads into values.

    from finnsdk import FinnClient

    with FinnClient() as finn:
        print(finn.state().battery_pct)
        finn.set_state(volume=40)
        finn.launch("notes")
"""

import os

from .protocol import (
    COMP_SOCKET,
    CONTROL_SOCKET,
    PROTOCOL_MAGIC,
    ControlSocket,
    ProtocolError,
    default_socket,
)


class FinnError(Exception):
    """The device refused, or could not be reached."""


class FinnState:
    """The system state, as the device reports it.

    Every field has an absent value rather than a default, because the
    point of the state is to say what the machine can actually do. A
    battery of -1 means there is no battery; a wireless of -1 means there
    is no adapter.
    """

    __slots__ = (
        "battery_pct", "charging", "wifi", "volume", "brightness",
        "bluetooth", "wired", "dark",
    )

    def __init__(self, **kw):
        for name in self.__slots__:
            setattr(self, name, kw.get(name, -1))

    @classmethod
    def from_payload(cls, payload):
        """Read the key=value payload of a `state.get` reply."""
        values = {}
        for token in payload.split():
            if "=" not in token:
                continue
            key, _, raw = token.partition("=")
            try:
                values[key] = int(raw)
            except ValueError:
                values[key] = raw
        # The protocol says `battery`; the field is `battery_pct`,
        # because what the device reports is a percentage.
        if "battery" in values:
            values["battery_pct"] = values.pop("battery")
        return cls(**values)

    def as_dict(self):
        return {name: getattr(self, name) for name in self.__slots__}

    def __repr__(self):
        body = ", ".join("%s=%s" % kv for kv in sorted(self.as_dict().items()))
        return "FinnState(%s)" % body

    def __eq__(self, other):
        return isinstance(other, FinnState) and self.as_dict() == other.as_dict()


class FinnApp:
    """One app in the registry."""

    __slots__ = ("id", "name", "state")

    def __init__(self, id, name, state):
        self.id = id
        self.name = name
        self.state = state

    @property
    def installed(self):
        return self.state == "installed"

    def __repr__(self):
        return "FinnApp(%r, %r, %r)" % (self.id, self.name, self.state)


class FinnClient:
    """
    A connection to a running FinnOS.

    The client talks to the control plane. If the control plane is not
    running it falls back to the compositor's socket, which is the same
    protocol with fewer verbs — enough to move the pointer and take a
    screenshot, which is what a test harness needs.
    """

    def __init__(self, path=None, timeout=10.0, direct=False):
        self.path = path or (COMP_SOCKET if direct else default_socket())
        self.timeout = timeout
        self.direct = direct
        self._ctl = None

    # ---- plumbing ----

    def _socket(self):
        if self._ctl is None:
            self._ctl = ControlSocket(self.path, self.timeout)
            try:
                self._ctl.connect()
            except OSError:
                if self.direct:
                    raise
                # The control plane is not running: the compositor's own
                # socket is the provider underneath it.
                self._ctl = ControlSocket(COMP_SOCKET, self.timeout)
                self._ctl.connect()
        return self._ctl

    def close(self):
        if self._ctl is not None:
            self._ctl.close()
            self._ctl = None

    def __enter__(self):
        return self

    def __exit__(self, *exc):
        self.close()
        return False

    def call(self, verb, *args, **kwargs):
        """
        Send one verb and return its payload.

        The payload is the text after "OK ", or None when the reply was a
        bare "OK". An "ERR" reply raises FinnError with the message the
        device gave.

        The protocol has no quoting: a request is a line, and an argument
        is a word. A title that contains a space goes in as a pipe
        separated pair — `notify.post "Title|Body"` — which is why the
        device can read it without a parser.
        """
        line = verb
        if args:
            line += " " + " ".join(str(a) for a in args)
        if kwargs:
            line += " " + " ".join("%s=%s" % (k, v) for k, v in kwargs.items())

        ctl = self._socket()
        ctl.send(line)
        reply = ctl.read_line()

        if reply.startswith("ERR"):
            raise FinnError(reply[4:].strip() or reply)
        if reply == "OK":
            return None
        if reply.startswith("OK "):
            return reply[3:].strip()
        raise ProtocolError("unexpected reply: %r" % reply)

    def call_binary(self, verb, *args, **kwargs):
        """Send one verb that answers with a binary payload."""
        line = verb
        if args:
            line += " " + " ".join(str(a) for a in args)
        if kwargs:
            line += " " + " ".join("%s=%s" % (k, v) for k, v in kwargs.items())

        ctl = self._socket()
        ctl.send(line)
        reply = ctl.read_line()
        if reply.startswith("ERR"):
            raise FinnError(reply[4:].strip() or reply)
        if not reply.startswith("OK BINARY "):
            raise ProtocolError("expected a binary reply, got %r" % reply)
        count = int(reply[len("OK BINARY "):].strip())
        return ctl.read_exactly(count)

    # ---- the verbs ----

    def ping(self):
        """The protocol's name and version."""
        return self.call("ping")

    def version(self):
        """What is on the other end."""
        return self.call("version")

    def state(self):
        """The system state."""
        return FinnState.from_payload(self.call("state.get") or "")

    def set_state(self, **values):
        """Write state overrides. `battery=42`, `volume=30`, ..."""
        return self.call("state.set", **values)

    def clear_state(self):
        """Drop the overrides; back to the real hardware."""
        return self.call("state.clear")

    def screen_info(self):
        """The framebuffer size and the density."""
        out = {}
        for token in (self.call("screen.info") or "").split():
            if "=" in token:
                key, _, value = token.partition("=")
                out[key] = value
        return out

    def screen_shell(self):
        """What the shell is showing: which surfaces are open, and so on."""
        out = {}
        for token in (self.call("screen.shell") or "").split():
            if "=" in token:
                key, _, value = token.partition("=")
                out[key] = value
        return out

    def capture(self, step=1):
        """A PPM of the frame, cursor included."""
        return self.call_binary("screen.capture", step=step)

    def pointer(self):
        """Where the pointer is."""
        out = {}
        for token in (self.call("screen.pointer") or "").split():
            if "=" in token:
                key, _, value = token.partition("=")
                out[key] = value
        return out

    def move(self, x, y):
        """Move the pointer."""
        return self.call("pointer.move", x=x, y=y)

    def move_by(self, dx, dy):
        """Nudge the pointer."""
        return self.call("pointer.by", dx=dx, dy=dy)

    def click(self, button="left"):
        """Click."""
        return self.call("pointer.click", button=button)

    def press(self, button="left"):
        return self.call("pointer.down", button=button)

    def release(self, button="left"):
        return self.call("pointer.up", button=button)

    def sound(self):
        """The output level and the mixer element it is on."""
        out = {}
        for token in (self.call("sound.get") or "").split():
            if "=" in token:
                key, _, value = token.partition("=")
                out[key] = value
        return out

    def set_volume(self, volume):
        return self.call("sound.set", volume=volume)

    def display(self):
        out = {}
        for token in (self.call("display.get") or "").split():
            if "=" in token:
                key, _, value = token.partition("=")
                out[key] = value
        return out

    def set_brightness(self, brightness):
        return self.call("display.set", brightness=brightness)

    def set_radio(self, wifi=None, bluetooth=None):
        kwargs = {}
        if wifi is not None:
            kwargs["wifi"] = 1 if wifi else 0
        if bluetooth is not None:
            kwargs["bluetooth"] = 1 if bluetooth else 0
        if not kwargs:
            raise ValueError("nothing to set")
        return self.call("radio.set", **kwargs)

    def set_appearance(self, dark):
        """Switch the appearance. `dark=True` for the dark one."""
        return self.call("appearance.set", dark=1 if dark else 0)

    def notify(self, title, body):
        """Post a notification."""
        return self.call("notify.post", "%s|%s" % (title, body))

    def notifications(self):
        """What has happened this session."""
        payload = self.call("notify.list") or ""
        out = []
        for chunk in payload.split("[")[1:]:
            chunk = chunk.strip().rstrip("]")
            parts = chunk.split("|")
            if len(parts) >= 3:
                out.append({"title": parts[0], "body": parts[1], "at": parts[2]})
        return out

    def clear_notifications(self):
        return self.call("notify.clear")

    def power(self, what="restart"):
        """`shutdown`, `restart` or `restart-comp`."""
        return self.call("power", what)

    def apps(self):
        """The application registry: what exists and what is installed."""
        payload = self.call("app.list") or ""
        out = []
        for chunk in payload.split("[")[1:]:
            chunk = chunk.strip().rstrip("]")
            parts = chunk.split("|")
            if len(parts) >= 3:
                out.append(FinnApp(parts[0], parts[2], parts[1]))
        return out

    def launch(self, app_id):
        """Start an app, as the user."""
        return self.call("app.launch", app_id)

    # ---- windows ----

    def window_list(self):
        """The windows the compositor is showing.

        Each reply entry is `[id|app|state|title]`; the title is last
        because it is the only field that may contain anything at all.
        """
        payload = self.call("window.list") or ""
        out = []
        for chunk in payload.split("[")[1:]:
            chunk = chunk.strip().rstrip("]")
            parts = chunk.split("|")
            if len(parts) < 4:
                continue
            try:
                win_id = int(parts[0])
            except ValueError:
                continue
            out.append({
                "id": win_id,
                "app": parts[1],
                "title": parts[3],
                "state": parts[2],
            })
        return out

    def window_open(self, app="", title=""):
        """Open a window, and return its id.

        `app` and `title` are optional: the protocol's words are optional
        too, and a window with neither is a window the shell decides how
        to label.
        """
        kwargs = {}
        if app:
            kwargs["app"] = app
        if title:
            kwargs["title"] = title
        payload = self.call("window.open", **kwargs) or ""
        return _int_field(payload, "id", None)

    def window_close(self, win_id):
        return self.call("window.close", id=win_id)

    def window_focus(self, win_id):
        return self.call("window.focus", id=win_id)

    def window_minimize(self, win_id):
        return self.call("window.minimize", id=win_id)

    def window_maximize(self, win_id):
        return self.call("window.maximize", id=win_id)

    def window_move(self, win_id, x, y):
        return self.call("window.move", id=win_id, x=x, y=y)

    # ---- setup ----

    def setup_get(self):
        """Where the first-run setup is.

        `active` and `done` are absent values when the device does not say
        them, rather than a guess that the setup is finished.
        """
        payload = self.call("setup.get") or ""
        values = {}
        for token in payload.split():
            if "=" not in token:
                continue
            key, _, raw = token.partition("=")
            values[key] = raw
        lang = values.get("lang", "")
        return {
            "active": _to_int(values.get("active")),
            "done": _to_int(values.get("done")),
            "lang": lang,
        }

    def setup_show(self):
        """Ask the shell to show the setup pages."""
        return self.call("setup.show")

    def setup_complete(self):
        """Tell the shell the setup has been finished."""
        return self.call("setup.complete")


def _to_int(raw, absent=-1):
    """A protocol field as an int, or the absent value."""
    try:
        return int(raw)
    except (TypeError, ValueError):
        return absent


def _int_field(payload, key, absent=-1):
    """Read one `key=value` field out of a reply payload."""
    for token in payload.split():
        name, sep, raw = token.partition("=")
        if sep and name == key:
            return _to_int(raw, absent)
    return absent


def connect(path=None, timeout=10.0, direct=False):
    """Open a client. A convenience for scripts."""
    return FinnClient(path=path, timeout=timeout, direct=direct)
