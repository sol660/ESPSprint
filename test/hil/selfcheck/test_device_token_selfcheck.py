"""Self-check for the device token (#24): the PC helper and the board token test.

The PC helper must send secrets.h's DEVICE_TOKEN as `x-device-token`. The board
token test must pass only when Supabase refuses an insert with no token or a
wrong one, for the real device id, and takes the same row with the right token;
rules that let such an insert through, or that refuse every insert, must make
it FAIL (ADR 0001).
"""

import http.server
import importlib.util
import threading
from pathlib import Path

import pytest

import harness
import supabase_rest
from fake_supabase import FakeSupabase

EXAMPLE = harness.ROOT / "include" / "secrets.example.h"
TOKEN_TEST_FILE = Path(__file__).resolve().parent.parent / "board" / "test_device_token.py"
TOKEN_TEST = "test_insert_without_the_device_token_or_with_a_wrong_one_is_refused"


def filled_in_secrets(tmp_path, token="0123abcd"):
    header = tmp_path / "secrets.h"
    header.write_text(EXAMPLE.read_text(encoding="utf-8")
                      .replace("https://your-project.supabase.co", "https://abc.supabase.co")
                      .replace("your-anon-or-publishable-key", "sb_publishable_xyz")
                      .replace("your-device-token", token),
                      encoding="utf-8")
    return header


# ---- secrets.h and the header on the wire ----

def test_the_device_token_is_read_from_the_firmware_header(tmp_path):
    db = supabase_rest.from_secrets(filled_in_secrets(tmp_path, token="feedbeef42"))

    assert db.token == "feedbeef42"


def test_secrets_without_a_device_token_are_refused(tmp_path):
    header = filled_in_secrets(tmp_path)
    header.write_text("\n".join(line for line in header.read_text(encoding="utf-8").splitlines()
                                if "DEVICE_TOKEN" not in line), encoding="utf-8")

    with pytest.raises(ValueError, match="DEVICE_TOKEN"):
        supabase_rest.from_secrets(header)


class Recorder(http.server.BaseHTTPRequestHandler):
    """Answers every request with 201 and keeps its headers in `seen`."""
    seen = []

    def _answer(self):
        length = int(self.headers.get("Content-Length") or 0)
        self.rfile.read(length)
        Recorder.seen.append({k.lower(): v for k, v in self.headers.items()})
        self.send_response(201)
        self.send_header("Content-Length", "0")
        self.end_headers()

    do_POST = do_GET = _answer

    def log_message(self, *args):
        pass


@pytest.fixture
def recording_server():
    Recorder.seen = []
    server = http.server.HTTPServer(("127.0.0.1", 0), Recorder)
    thread = threading.Thread(target=server.serve_forever, daemon=True)
    thread.start()
    try:
        yield f"http://127.0.0.1:{server.server_address[1]}", Recorder.seen
    finally:
        server.shutdown()
        server.server_close()


def test_inserts_carry_the_device_token_header(recording_server):
    url, seen = recording_server
    db = supabase_rest.SupabaseRest(url, "sb_publishable_xyz", "node-01", token="feedbeef42")

    status, _ = db.insert({"seq": 1})

    assert status == 201
    assert seen[-1].get("x-device-token") == "feedbeef42"
    assert seen[-1].get("apikey") == "sb_publishable_xyz"


def test_a_copy_with_no_token_sends_no_header(recording_server):
    url, seen = recording_server
    db = supabase_rest.SupabaseRest(url, "sb_publishable_xyz", "node-01", token="feedbeef42")

    db.with_token(None).insert({"seq": 1})

    assert "x-device-token" not in seen[-1]
    assert db.token == "feedbeef42", "with_token must not change the original"


def test_a_copy_with_another_token_sends_that_one(recording_server):
    url, seen = recording_server
    db = supabase_rest.SupabaseRest(url, "sb_publishable_xyz", "node-01", token="feedbeef42")

    db.with_token("not-the-token").insert({"seq": 1})

    assert seen[-1].get("x-device-token") == "not-the-token"
