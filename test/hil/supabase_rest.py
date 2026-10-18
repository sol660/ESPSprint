"""Supabase's REST API from the PC, with the device's own key (spec #14, story 41).

The URL, key and device ID come from include/secrets.h, the same file the
firmware is built with, so secrets live in one place. Only the standard
library is used.
"""

import json
import re
import urllib.error
import urllib.parse
import urllib.request

from harness import ROOT

SECRETS = ROOT / "include" / "secrets.h"
PLACEHOLDERS = ("your-project", "your-anon", "your-device")


def read_secrets(path=SECRETS):
    """The `constexpr const char* NAME = "value";` strings in secrets.h, as a dict."""
    text = path.read_text(encoding="utf-8")
    return dict(re.findall(r'const\s+char\s*\*\s*(\w+)\s*=\s*"([^"]*)"', text))


class SupabaseRest:
    """The readings table, seen with the device's (insert-only) key."""

    def __init__(self, url, key, device_id, timeout_s=20):
        self.base = url.rstrip("/") + "/rest/v1/readings"
        self.key = key
        self.device_id = device_id
        self.timeout_s = timeout_s

    def _headers(self, prefer=None):
        h = {"apikey": self.key, "Content-Type": "application/json"}
        if self.key.startswith("eyJ"):  # legacy anon key (a JWT); the firmware does the same
            h["Authorization"] = "Bearer " + self.key
        if prefer:
            h["Prefer"] = prefer
        return h

    def _call(self, method, query="", body=None, prefer=None):
        """(HTTP status, parsed JSON or raw text). HTTP errors are returned, not raised."""
        url = self.base + ("?" + query if query else "")
        data = None if body is None else json.dumps(body).encode()
        req = urllib.request.Request(url, data=data, method=method, headers=self._headers(prefer))
        try:
            with urllib.request.urlopen(req, timeout=self.timeout_s) as resp:
                status, raw = resp.status, resp.read().decode()
        except urllib.error.HTTPError as e:
            status, raw = e.code, e.read().decode(errors="replace")
        try:
            return status, json.loads(raw) if raw else None
        except ValueError:
            return status, raw

    @staticmethod
    def _tag_filter(tag):
        return "test_tag=eq." + urllib.parse.quote(str(tag))

    def rows_with_tag(self, tag):
        status, rows = self._call("GET", self._tag_filter(tag) + "&select=*")
        if status != 200 or not isinstance(rows, list):
            raise RuntimeError(f"Supabase select failed: HTTP {status} {rows}")
        return rows

    def insert(self, row):
        return self._call("POST", body=row, prefer="return=minimal")

    def update_tagged(self, tag, values):
        """Tries to change the tagged rows; (status, rows it says it changed)."""
        return self._call("PATCH", self._tag_filter(tag), body=values,
                          prefer="return=representation")

    def delete_tagged(self, tag):
        """Tries to delete the tagged rows; (status, rows it says it deleted)."""
        return self._call("DELETE", self._tag_filter(tag), prefer="return=representation")


def from_secrets(path=SECRETS):
    """A SupabaseRest built from include/secrets.h; raises ValueError if it isn't filled in."""
    if not path.exists():
        raise ValueError(f"{path} is missing: copy include/secrets.example.h to it and fill in "
                         "the Supabase URL and key (supabase/SETUP.md)")
    s = read_secrets(path)
    missing = [k for k in ("SUPABASE_URL", "SUPABASE_KEY", "DEVICE_ID") if not s.get(k)]
    if missing:
        raise ValueError(f"{path} has no {', '.join(missing)}")
    if any(p in s["SUPABASE_URL"] + s["SUPABASE_KEY"] for p in PLACEHOLDERS):
        raise ValueError(f"{path} still has the example Supabase URL/key: see supabase/SETUP.md")
    return SupabaseRest(s["SUPABASE_URL"], s["SUPABASE_KEY"], s["DEVICE_ID"])
