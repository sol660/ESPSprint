"""ntfy's JSON poll API from the PC, for the board alert test (#20; spec #14 story 53).

The topic comes from include/secrets.h (NTFY_TOPIC) and the server from
include/config.h (NTFY_SERVER), the same values the firmware is built with.
Only the standard library is used.
"""

import json
import re
import time
import urllib.parse
import urllib.request

from harness import ROOT
from supabase_rest import SECRETS, read_secrets

CONFIG = ROOT / "include" / "config.h"
PLACEHOLDER = "change-me"  # in secrets.example.h's topic


def ntfy_server(path=CONFIG):
    """config::NTFY_SERVER from include/config.h."""
    m = re.search(r'NTFY_SERVER\s*=\s*"([^"]+)"', path.read_text(encoding="utf-8"))
    assert m, f"NTFY_SERVER not found in {path}"
    return m.group(1)


class NtfyRest:
    """One ntfy topic, read through the JSON poll API."""

    def __init__(self, server, topic, timeout_s=20):
        self.server = server.rstrip("/")
        self.topic = topic
        self.timeout_s = timeout_s

    def messages_since(self, since_unix):
        """Messages the server has cached for the topic since since_unix (whole seconds),
        as dicts with at least `time` (server unix time) and `message`."""
        query = urllib.parse.urlencode({"poll": 1, "since": int(since_unix)})
        url = f"{self.server}/{urllib.parse.quote(self.topic)}/json?{query}"
        with urllib.request.urlopen(url, timeout=self.timeout_s) as resp:
            if resp.status != 200:
                raise RuntimeError(f"ntfy poll failed: HTTP {resp.status}")
            lines = resp.read().decode("utf-8").splitlines()
        msgs = [json.loads(line) for line in lines if line.strip()]
        return [m for m in msgs if m.get("event") == "message"]

    def pause(self, seconds):
        """Waits between polls (a fake makes this instant)."""
        time.sleep(seconds)


def from_secrets(path=SECRETS, config=CONFIG):
    """An NtfyRest for secrets::NTFY_TOPIC; raises ValueError if it isn't filled in."""
    if not path.exists():
        raise ValueError(f"{path} is missing: copy include/secrets.example.h to it and set "
                         "NTFY_TOPIC")
    topic = read_secrets(path).get("NTFY_TOPIC")
    if not topic:
        raise ValueError(f"{path} has no NTFY_TOPIC")
    if PLACEHOLDER in topic:
        raise ValueError(f"{path} still has the example NTFY_TOPIC: set a hard-to-guess topic "
                         "and subscribe to it in the ntfy app")
    return NtfyRest(ntfy_server(config), topic)
