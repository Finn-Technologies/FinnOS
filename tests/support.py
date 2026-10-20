"""The paths and the helpers the tests share.

The SDK is not installed when the tests run, so the SDK directory goes
on the path here rather than in every test module.
"""

import contextlib
import io
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.dirname(HERE)
SDK = os.path.join(REPO, "sdk")

if SDK not in sys.path:
    sys.path.insert(0, SDK)

UI_DIR = os.path.join(REPO, "ui")
APPS_DIR = os.path.join(REPO, "apps")
TEMPLATES_DIR = os.path.join(SDK, "templates")


def fui_files():
    """Every `.fui` source in the tree, in a stable order."""
    out = []
    for directory in (UI_DIR, APPS_DIR):
        for root, dirs, names in os.walk(directory):
            dirs.sort()
            for name in sorted(names):
                if name.endswith(".fui"):
                    out.append(os.path.join(root, name))
    return sorted(out)


def relative(path):
    """A path as the repo would say it."""
    return os.path.relpath(path, REPO)


@contextlib.contextmanager
def quiet():
    """Hold a command's chatter, so the test output stays readable.

    It yields the buffer, so a test can still say what the command
    printed.
    """
    buf = io.StringIO()
    with contextlib.redirect_stdout(buf), contextlib.redirect_stderr(buf):
        yield buf
