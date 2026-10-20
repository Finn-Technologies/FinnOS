"""
findsdk — the toolkit for building on FinnOS.

Two things live here:

    findsdk.client     the control-plane client: talk to a running FinnOS
    findsdk.fuic       the compiler: .fui in, .fbin out

The client speaks the same line protocol the device does, so a script that
uses it and a script that uses `finnctl` over ssh are doing the same
thing. The compiler turns the UI language's source into the binary form
the system loads.

Nothing here needs a third-party package. The SDK has to run on whatever
host the agent runs on, and a toolkit that needs a wheel to answer a
question about a window is a toolkit nobody will use.
"""

__version__ = "0.1.0"

from .client import FinnClient, FinnError, FinnState
from .protocol import PROTOCOL_MAGIC, ControlSocket

__all__ = [
    "FinnClient",
    "FinnError",
    "FinnState",
    "ControlSocket",
    "PROTOCOL_MAGIC",
    "__version__",
]
