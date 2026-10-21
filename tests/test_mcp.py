"""The MCP adapter: the tools it offers, and what they do.

The adapter is one file with no dependencies, and it runs on the host,
not on the device. These tests load it, check the tool list, and drive
the application, window and setup tools against a stub instead of a device.
"""

import importlib.machinery
import importlib.util
import json
import os
import re
import sys
import unittest

# The tests share a helper module, and the SDK is not installed when
# they run: both live beside this file.
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

import support  # noqa: E402,F401  (puts the SDK on the path)

MCP = os.path.join(support.REPO, "mcp", "finn-mcp")


def load_adapter():
    loader = importlib.machinery.SourceFileLoader("finn_mcp", MCP)
    spec = importlib.util.spec_from_loader("finn_mcp", loader)
    module = importlib.util.module_from_spec(spec)
    loader.exec_module(module)
    return module


class ToolListTest(unittest.TestCase):

    def setUp(self):
        self.adapter = load_adapter()

    def test_every_tool_has_a_name_a_description_and_a_handler(self):
        for tool in self.adapter.TOOLS:
            self.assertTrue(tool["name"].startswith("finn_"), tool["name"])
            self.assertTrue(tool["description"].strip(), tool["name"])
            self.assertTrue(callable(tool["handler"]), tool["name"])
            self.assertEqual(tool["inputSchema"]["type"], "object")

    def test_the_tool_names_are_unique(self):
        names = [tool["name"] for tool in self.adapter.TOOLS]
        self.assertEqual(len(names), len(set(names)))

    def test_the_tools_include_the_application_ones(self):
        names = [tool["name"] for tool in self.adapter.TOOLS]
        self.assertIn("finn_app_list", names)
        self.assertIn("finn_app_launch", names)

    def test_the_tools_include_the_window_and_setup_ones(self):
        names = [tool["name"] for tool in self.adapter.TOOLS]
        for name in ("finn_window_list", "finn_window_open",
                     "finn_window_close", "finn_window_focus",
                     "finn_window_minimize", "finn_window_maximize",
                     "finn_window_move", "finn_setup_get",
                     "finn_setup_show", "finn_setup_complete"):
            self.assertIn(name, names)

    def test_the_count_in_the_readme_is_the_count_of_the_tools(self):
        with open(os.path.join(support.REPO, "mcp", "README.md"), "r",
                  encoding="utf-8") as handle:
            readme = handle.read()
        found = re.findall(r"tools:\s*(\d+)", readme)
        self.assertTrue(found, "the README does not say how many tools there are")
        self.assertEqual(int(found[0]), len(self.adapter.TOOLS))

    def test_every_tool_is_in_the_readme(self):
        with open(os.path.join(support.REPO, "mcp", "README.md"), "r",
                  encoding="utf-8") as handle:
            readme = handle.read()
        for tool in self.adapter.TOOLS:
            self.assertIn(tool["name"], readme, tool["name"])


class JsonRpcTest(unittest.TestCase):

    def setUp(self):
        self.adapter = load_adapter()

    def reply_to(self, message):
        lines = []
        original = self.adapter.send

        def capture(msg):
            lines.append(json.dumps(msg))

        self.adapter.send = capture
        try:
            self.adapter.handle(message)
        finally:
            self.adapter.send = original
        return json.loads(lines[0]) if lines else None

    def test_the_tools_list_has_no_handlers_in_it(self):
        reply = self.reply_to({"jsonrpc": "2.0", "id": 1, "method": "tools/list"})
        tools = reply["result"]["tools"]
        self.assertEqual(len(tools), len(self.adapter.TOOLS))
        for tool in tools:
            self.assertNotIn("handler", tool)

    def test_an_unknown_tool_is_an_error(self):
        reply = self.reply_to({
            "jsonrpc": "2.0", "id": 2, "method": "tools/call",
            "params": {"name": "finn_nonesuch", "arguments": {}},
        })
        self.assertIn("error", reply)

    def test_initialize_answers_with_the_protocol(self):
        reply = self.reply_to({
            "jsonrpc": "2.0", "id": 3, "method": "initialize",
            "params": {"protocolVersion": "2025-06-18"},
        })
        self.assertEqual(reply["result"]["protocolVersion"], "2025-06-18")
        self.assertEqual(reply["result"]["serverInfo"]["name"], "finnos")


class AppToolsTest(unittest.TestCase):
    """The application tools, against a stub instead of a device."""

    def setUp(self):
        self.adapter = load_adapter()
        self.calls = []
        self.original = self.adapter.ctl

        def stub(*args):
            self.calls.append(list(args))
            return (True, self.reply_for(args), "")

        self.adapter.ctl = stub
        self.addCleanup(self.restore)

    def restore(self):
        self.adapter.ctl = self.original

    def reply_for(self, args):
        if args[0] == "app.list":
            return ("count=2 [notes|installed|Finn Notes]"
                    " [code|available|Finn Code]")
        if args[0] == "app.launch":
            return "launched %s" % args[1]
        return ""

    def test_the_app_list_says_what_is_installed(self):
        text, is_error = self.adapter.tool_app_list({})
        self.assertFalse(is_error)
        self.assertIn("notes", text)
        self.assertIn("installed", text)
        self.assertIn("Finn Code", text)
        self.assertIn("available", text)

    def test_the_app_list_calls_the_registry(self):
        self.adapter.tool_app_list({})
        self.assertEqual(self.calls, [["app.list"]])

    def test_an_app_is_launched_by_its_id(self):
        text, is_error = self.adapter.tool_app_launch({"id": "notes"})
        self.assertFalse(is_error)
        self.assertIn("launched notes", text)
        self.assertEqual(self.calls, [["app.launch", "notes"]])

    def test_launching_without_an_id_is_an_error(self):
        text, is_error = self.adapter.tool_app_launch({})
        self.assertTrue(is_error)

    def test_a_device_that_refuses_is_an_error(self):
        self.adapter.ctl = lambda *args: (False, "", "ERR not installed")
        text, is_error = self.adapter.tool_app_launch({"id": "notes"})
        self.assertTrue(is_error)
        self.assertIn("not installed", text)


class WindowToolsTest(unittest.TestCase):
    """The window tools, against a stub instead of a device."""

    def setUp(self):
        self.adapter = load_adapter()
        self.calls = []
        self.original = self.adapter.ctl

        def stub(*args):
            self.calls.append(list(args))
            return (True, self.reply_for(args), "")

        self.adapter.ctl = stub
        self.addCleanup(self.restore)

    def restore(self):
        self.adapter.ctl = self.original

    def reply_for(self, args):
        if args[0] == "window.list":
            return ("count=2 [1|notes|normal|Finn Notes]"
                    " [2|code|maximized|Finn Code]")
        if args[0] == "window.open":
            return "id=3"
        return ""

    def test_the_window_list_says_what_is_open(self):
        text, is_error = self.adapter.tool_window_list({})
        self.assertFalse(is_error)
        self.assertIn("notes", text)
        self.assertIn("Finn Notes", text)
        self.assertIn("normal", text)
        self.assertIn("maximized", text)
        self.assertEqual(self.calls, [["window.list"]])

    def test_a_window_is_opened_with_an_app_and_a_title(self):
        text, is_error = self.adapter.tool_window_open(
            {"app": "notes", "title": "Finn Notes"})
        self.assertFalse(is_error)
        self.assertIn("id=3", text)
        self.assertEqual(self.calls,
                         [["window.open", "app=notes", "title=Finn Notes"]])

    def test_a_window_may_be_opened_from_nothing(self):
        _text, is_error = self.adapter.tool_window_open({})
        self.assertFalse(is_error)
        self.assertEqual(self.calls, [["window.open"]])

    def test_a_window_is_closed_by_its_id(self):
        _text, is_error = self.adapter.tool_window_close({"id": 1})
        self.assertFalse(is_error)
        self.assertEqual(self.calls, [["window.close", "id=1"]])

    def test_a_window_is_focused_by_its_id(self):
        _text, is_error = self.adapter.tool_window_focus({"id": 2})
        self.assertFalse(is_error)
        self.assertEqual(self.calls, [["window.focus", "id=2"]])

    def test_a_window_is_minimized_by_its_id(self):
        _text, is_error = self.adapter.tool_window_minimize({"id": 1})
        self.assertFalse(is_error)
        self.assertEqual(self.calls, [["window.minimize", "id=1"]])

    def test_a_window_is_maximized_by_its_id(self):
        _text, is_error = self.adapter.tool_window_maximize({"id": 1})
        self.assertFalse(is_error)
        self.assertEqual(self.calls, [["window.maximize", "id=1"]])

    def test_a_window_is_moved_by_its_id_and_coordinate(self):
        _text, is_error = self.adapter.tool_window_move({"id": 1, "x": 40, "y": 60})
        self.assertFalse(is_error)
        self.assertEqual(self.calls, [["window.move", "id=1", "x=40", "y=60"]])

    def test_a_window_action_without_an_id_is_an_error(self):
        text, is_error = self.adapter.tool_window_close({})
        self.assertTrue(is_error)
        self.assertEqual(self.calls, [])

    def test_moving_without_a_coordinate_is_an_error(self):
        _text, is_error = self.adapter.tool_window_move({"id": 1, "x": 40})
        self.assertTrue(is_error)
        self.assertEqual(self.calls, [])


class SetupToolsTest(unittest.TestCase):
    """The first-run setup tools, against a stub instead of a device."""

    def setUp(self):
        self.adapter = load_adapter()
        self.calls = []
        self.original = self.adapter.ctl

        def stub(*args):
            self.calls.append(list(args))
            if args[0] == "setup.get":
                return (True, "active=1 done=0 lang=en", "")
            return (True, "", "")

        self.adapter.ctl = stub
        self.addCleanup(self.restore)

    def restore(self):
        self.adapter.ctl = self.original

    def test_the_setup_state_is_reported(self):
        text, is_error = self.adapter.tool_setup_get({})
        self.assertFalse(is_error)
        self.assertIn("active: yes", text)
        self.assertIn("done: no", text)
        self.assertIn("language: en", text)
        self.assertEqual(self.calls, [["setup.get"]])

    def test_a_missing_setup_field_stays_unknown(self):
        self.adapter.ctl = lambda *args: (True, "lang=fr", "")
        text, is_error = self.adapter.tool_setup_get({})
        self.assertFalse(is_error)
        self.assertIn("active: unknown", text)
        self.assertIn("done: unknown", text)
        self.assertIn("language: fr", text)

    def test_the_setup_may_be_shown(self):
        _text, is_error = self.adapter.tool_setup_show({})
        self.assertFalse(is_error)
        self.assertEqual(self.calls, [["setup.show"]])

    def test_the_setup_may_be_completed(self):
        _text, is_error = self.adapter.tool_setup_complete({})
        self.assertFalse(is_error)
        self.assertEqual(self.calls, [["setup.complete"]])


if __name__ == "__main__":
    unittest.main()
