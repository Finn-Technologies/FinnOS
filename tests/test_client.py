"""The client, against a fake control plane.

The device is a VM; the tests cannot need one. `fakeserver` speaks the
same line protocol over a unix socket in this process, so these tests
cover the client's half: the requests it sends, the replies it reads,
and what it does when the device refuses.
"""

import os
import sys
import unittest

# The tests share a helper module, and the SDK is not installed when
# they run: both live beside this file.
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

import support  # noqa: E402,F401  (puts the SDK on the path)

import fakeserver

from finnsdk import FinnClient, FinnError, FinnState
from finnsdk.protocol import ControlSocket, ProtocolError


class ClientTest(unittest.TestCase):

    def setUp(self):
        self.server = fakeserver.FakeServer()
        self.addCleanup(self.server.close)
        self.client = FinnClient(path=self.server.path, timeout=5.0)
        self.addCleanup(self.client.close)

    # ---- the session ----

    def test_ping_answers_with_the_protocol(self):
        self.assertEqual(self.client.ping(), "FINNCTL/1")
        self.assertEqual(self.server.requests, ["ping"])

    def test_a_connection_carries_many_requests(self):
        self.client.ping()
        self.client.state()
        self.client.ping()
        self.assertEqual(len(self.server.requests), 3)

    # ---- the state ----

    def test_the_state_is_read_as_values(self):
        state = self.client.state()
        self.assertEqual(state.battery_pct, 87)
        self.assertEqual(state.wifi, 3)
        self.assertEqual(state.volume, 70)
        self.assertEqual(state.bluetooth, -1)
        self.assertEqual(self.server.requests, ["state.get"])

    def test_an_absent_value_stays_absent(self):
        # there is no bluetooth adapter on this machine, and -1 says so
        state = self.client.state()
        self.assertEqual(state.bluetooth, -1)
        self.assertIn("bluetooth", state.as_dict())

    def test_the_state_may_be_overridden(self):
        self.client.set_state(battery=42, charging=1)
        self.assertEqual(self.server.requests, ["state.set battery=42 charging=1"])
        self.assertEqual(self.client.state().battery_pct, 42)

    def test_the_overrides_may_be_dropped(self):
        self.client.set_state(battery=42)
        self.client.clear_state()
        self.assertEqual(self.server.requests[-1], "state.clear")
        self.assertEqual(self.client.state().battery_pct, 87)

    def test_the_state_may_be_read_into_a_dict(self):
        state = self.client.state()
        self.assertEqual(state.as_dict()["volume"], 70)

    # ---- the registry ----

    def test_the_apps_are_read_with_their_state(self):
        apps = self.client.apps()
        self.assertEqual([app.id for app in apps], ["notes", "code", "files"])
        self.assertEqual(apps[0].name, "Finn Notes")
        self.assertEqual(apps[0].state, "installed")
        self.assertTrue(apps[0].installed)

    def test_an_app_that_is_not_installed_says_so(self):
        apps = self.client.apps()
        self.assertEqual(apps[1].state, "available")
        self.assertFalse(apps[1].installed)

    def test_an_app_may_be_launched(self):
        self.assertEqual(self.client.launch("notes"), "launched notes")
        self.assertEqual(self.server.requests, ["app.launch notes"])

    def test_launching_an_app_the_device_does_not_know_is_an_error(self):
        with self.assertRaises(FinnError):
            self.client.launch("nonesuch")

    # ---- the windows ----

    def test_the_windows_are_read_with_their_state(self):
        windows = self.client.window_list()
        self.assertEqual([w["id"] for w in windows], [1, 2])
        self.assertEqual(windows[0]["app"], "notes")
        self.assertEqual(windows[0]["title"], "Finn Notes")
        self.assertEqual(windows[0]["state"], "normal")
        self.assertEqual(windows[1]["state"], "maximized")
        self.assertEqual(self.server.requests, ["window.list"])

    def test_a_window_may_be_opened(self):
        self.assertEqual(self.client.window_open("notes", "Finn Notes"), 3)
        self.assertEqual(self.server.requests,
                         ["window.open app=notes title=Finn Notes"])

    def test_a_window_may_be_opened_from_nothing(self):
        self.assertEqual(self.client.window_open(), 3)
        self.assertEqual(self.server.requests, ["window.open"])

    def test_a_window_may_be_closed(self):
        self.client.window_close(1)
        self.assertEqual(self.server.requests, ["window.close id=1"])

    def test_a_window_may_be_focused(self):
        self.client.window_focus(2)
        self.assertEqual(self.server.requests, ["window.focus id=2"])

    def test_a_window_may_be_minimized(self):
        self.client.window_minimize(1)
        self.assertEqual(self.server.requests, ["window.minimize id=1"])

    def test_a_window_may_be_maximized(self):
        self.client.window_maximize(1)
        self.assertEqual(self.server.requests, ["window.maximize id=1"])

    def test_a_window_may_be_moved(self):
        self.client.window_move(1, 40, 60)
        self.assertEqual(self.server.requests, ["window.move id=1 x=40 y=60"])

    # ---- the setup ----

    def test_the_setup_is_read_as_values(self):
        setup = self.client.setup_get()
        self.assertEqual(setup, {"active": 1, "done": 0, "lang": "en"})
        self.assertEqual(self.server.requests, ["setup.get"])

    def test_setup_values_the_device_does_not_say_are_absent(self):
        self.server.handler = lambda request, server: "OK lang=fr"
        self.assertEqual(self.client.setup_get(),
                         {"active": -1, "done": -1, "lang": "fr"})

    def test_the_setup_may_be_shown(self):
        self.client.setup_show()
        self.assertEqual(self.server.requests, ["setup.show"])

    def test_the_setup_may_be_completed(self):
        self.client.setup_complete()
        self.assertEqual(self.server.requests, ["setup.complete"])

    # ---- the pointer ----

    def test_the_pointer_may_be_moved(self):
        self.client.move(300, 400)
        self.assertEqual(self.server.requests, ["pointer.move x=300 y=400"])

    def test_the_pointer_may_be_nudged(self):
        self.client.move_by(10, -4)
        self.assertEqual(self.server.requests, ["pointer.by dx=10 dy=-4"])

    def test_the_pointer_may_be_clicked(self):
        self.client.click()
        self.assertEqual(self.server.requests, ["pointer.click button=left"])

    def test_a_key_may_be_pressed_released_and_tapped(self):
        self.client.key_press("shift")
        self.client.key_tap("tab")
        self.client.key_release("shift")
        self.assertEqual(
            self.server.requests,
            ["key.press code=shift", "key.tap code=tab", "key.release code=shift"],
        )

    # ---- the frame ----

    def test_the_frame_is_read_as_bytes(self):
        data = self.client.capture()
        self.assertTrue(data.startswith(b"P6"))
        self.assertEqual(len(data), len(fakeserver.PPM))

    def test_a_capture_that_is_not_a_capture_is_an_error(self):
        # the device answered with text where a frame should have been
        self.server.handler = lambda request, server: "OK not a frame"
        with self.assertRaises(ProtocolError):
            self.client.capture()

    # ---- the errors ----

    def test_a_refusal_is_an_error(self):
        self.server.handler = lambda request, server: "ERR not installed"
        with self.assertRaises(FinnError) as caught:
            self.client.launch("notes")
        self.assertEqual(str(caught.exception), "not installed")

    def test_a_reply_that_makes_no_sense_is_an_error(self):
        self.server.handler = lambda request, server: "WHAT"
        with self.assertRaises(ProtocolError):
            self.client.ping()

    def test_a_device_that_is_not_there_is_an_error(self):
        client = FinnClient(path="/nonexistent/control.sock", timeout=1.0)
        with self.assertRaises(OSError):
            client.ping()


class DirectTest(unittest.TestCase):
    """The compositor's socket is an explicit choice, never a fallback."""

    def setUp(self):
        import finnsdk.client as client_module
        self.client_module = client_module
        original = client_module.COMP_SOCKET
        self.addCleanup(lambda: setattr(client_module, "COMP_SOCKET", original))

    def test_a_client_does_not_use_the_compositor_behind_the_control_plane(self):
        # the control plane is down, and the compositor is right there: the
        # client still refuses, rather than taking the unaudited door
        comp = fakeserver.FakeServer()
        self.addCleanup(comp.close)
        self.client_module.COMP_SOCKET = comp.path

        with FinnClient(path="/nonexistent/control.sock", timeout=1.0) as finn:
            with self.assertRaises(OSError):
                finn.ping()
        self.assertEqual(comp.requests, [])

    def test_direct_connects_to_the_compositor_socket(self):
        comp = fakeserver.FakeServer()
        self.addCleanup(comp.close)
        self.client_module.COMP_SOCKET = comp.path

        with FinnClient(timeout=2.0, direct=True) as finn:
            self.assertEqual(finn.ping(), "FINNCTL/1")
        self.assertEqual(comp.requests, ["ping"])

    def test_direct_against_a_missing_socket_is_an_error(self):
        self.client_module.COMP_SOCKET = "/nonexistent/comp.sock"

        with FinnClient(timeout=1.0, direct=True) as finn:
            with self.assertRaises(OSError):
                finn.ping()


class StateTest(unittest.TestCase):
    """The state object itself, without a device."""

    def test_a_missing_field_is_absent_rather_than_zero(self):
        state = FinnState()
        self.assertEqual(state.battery_pct, -1)
        self.assertEqual(state.volume, -1)

    def test_a_payload_is_read_into_the_fields(self):
        state = FinnState.from_payload("battery=87 charging=0 wifi=3")
        self.assertEqual(state.battery_pct, 87)
        self.assertEqual(state.charging, 0)
        self.assertEqual(state.wifi, 3)
        self.assertEqual(state.volume, -1)

    def test_two_states_are_equal_when_they_say_the_same_thing(self):
        self.assertEqual(FinnState(battery_pct=1), FinnState(battery_pct=1))
        self.assertNotEqual(FinnState(battery_pct=1), FinnState(battery_pct=2))


class SocketTest(unittest.TestCase):

    def test_a_request_that_is_too_long_is_refused(self):
        server = fakeserver.FakeServer()
        self.addCleanup(server.close)
        with ControlSocket(server.path, timeout=5.0) as ctl:
            with self.assertRaises(ProtocolError):
                ctl.send("x" * 5000)


if __name__ == "__main__":
    unittest.main()
