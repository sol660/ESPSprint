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


# ---- the board token test, against a fake Supabase ----

RIGHT = "c0ffee00c0ffee00c0ffee00c0ffee00c0ffee00c0ffee00"


def load_token_tests():
    spec = importlib.util.spec_from_file_location("token_under_check", TOKEN_TEST_FILE)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def run_token_check(supabase):
    """Runs the board token test; returns the exception it raised, or None."""
    test = getattr(load_token_tests(), TOKEN_TEST)
    try:
        test(supabase)
    except BaseException as e:  # noqa: BLE001 - a skip must be caught and reported too
        return e
    return None


def assert_failed(result):
    assert result is not None, "token test PASSED against bad rules: guard broken"
    assert isinstance(result, (AssertionError, pytest.fail.Exception)), (
        f"token test must FAIL, but ended with {type(result).__name__}: {result}")


def checked_db(cls=FakeSupabase, device_id="node-01", token=RIGHT):
    """Rules that check the token: only `token`, stored for `device_id`, inserts its rows."""
    return cls(device_tokens={device_id: token}, token=token)


class AnyTokenWillDo(FakeSupabase):
    """Refuses a missing header, but takes any token at all."""

    def insert_with_token(self, row, token):
        return super().insert_with_token(row, RIGHT if token is not None else None)


class RefusesWithAnotherError(FakeSupabase):
    """A bad token gets 404 (for example a missing table), not the rules' refusal."""

    def insert_with_token(self, row, token):
        status, body = super().insert_with_token(row, token)
        if status == 401:
            return 404, {"code": "42P01", "message": 'relation "public.readings" does not exist'}
        return status, body


class StoresButSaysRefused(FakeSupabase):
    """Answers 401 for a bad token, yet the row goes in."""

    def insert_with_token(self, row, token):
        status, body = super().insert_with_token(row, token)
        if status == 401:
            self.rows.append(dict(row))
        return status, body


def test_token_check_passes_when_only_the_right_token_inserts():
    # Positive control: the failures below must come from the checks, not a test that can't pass.
    assert run_token_check(checked_db()) is None


def test_token_check_fails_when_tokens_are_not_checked():
    assert_failed(run_token_check(FakeSupabase(token=RIGHT)))


def test_token_check_fails_when_any_token_is_accepted():
    assert_failed(run_token_check(checked_db(AnyTokenWillDo)))


def test_token_check_fails_when_the_right_token_is_refused_too():
    # The token is stored for another device id, so every insert is refused.
    assert_failed(run_token_check(checked_db(device_id="node-02")))


def test_token_check_fails_when_the_refusal_is_another_error():
    assert_failed(run_token_check(checked_db(RefusesWithAnotherError)))


def test_token_check_fails_when_a_refused_row_is_stored_anyway():
    assert_failed(run_token_check(checked_db(StoresButSaysRefused)))


def test_token_check_fails_when_secrets_still_hold_the_example_token():
    assert_failed(run_token_check(checked_db(token="your-device-token")))


def test_token_check_fails_with_no_token_in_secrets():
    assert_failed(run_token_check(checked_db(token=None)))
