"""Self-check for the board upload tests (#19): runs them against a fake board and Supabase.

A healthy upload path must pass; missing, duplicate, old or wrong rows, a failed
upload, and a device key that can change or delete data must make the test
FAIL (ADR 0001).
"""

import importlib.util
from pathlib import Path

import pytest

import harness
import supabase_rest
from fake_serial import FakeFirmware, FakeSerial
from fake_supabase import FakeSupabase, UploadingBoard
from fake_tilt_firmware import FakeClock, StillBoard

UPLOAD = Path(__file__).resolve().parent.parent / "board" / "test_upload.py"
FW = harness.firmware_version()
WIFI_TEST = "test_wifi_uploads_exactly_one_row_with_this_runs_tag"
KEY_TEST = "test_device_key_can_insert_but_not_update_or_delete"


def load_upload_tests():
    spec = importlib.util.spec_from_file_location("upload_under_check", UPLOAD)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def outcome(call):
    try:
        call()
    except BaseException as e:  # noqa: BLE001 - a skip must be caught and reported too
        return e
    return None


def run_wifi(supabase=None, port=None, **board_kw):
    """Runs the board upload test; returns the exception it raised, or None."""
    supabase = supabase or FakeSupabase()
    port = port or UploadingBoard(FW, supabase, **board_kw)
    test = getattr(load_upload_tests(), WIFI_TEST)
    return outcome(lambda: test(harness.Board(port, clock=FakeClock(), sleep=lambda s: None),
                                supabase))


def run_key_check(supabase):
    test = getattr(load_upload_tests(), KEY_TEST)
    return outcome(lambda: test(supabase))


def assert_failed(result):
    assert result is not None, "upload test PASSED against a bad board/database: guard broken"
    assert isinstance(result, (AssertionError, pytest.fail.Exception)), (
        f"upload test must FAIL, but ended with {type(result).__name__}: {result}")


# ---- the wifi upload test ----

def test_passes_against_a_healthy_upload():
    # Positive control: the failures below must come from the checks, not a test that can't pass.
    assert run_wifi() is None


def test_passes_with_other_rows_in_the_table():
    db = FakeSupabase()
    db.insert({"test_tag": None, "seq": 1})
    db.insert({"test_tag": "111111111", "seq": 2})

    assert run_wifi(supabase=db) is None


def test_fails_when_no_row_has_the_tag():
    assert_failed(run_wifi(rows_uploaded=0))


def test_fails_when_two_rows_have_the_tag():
    assert_failed(run_wifi(rows_uploaded=2))


def test_fails_when_the_row_has_another_tag():
    assert_failed(run_wifi(row_tag="123456789"))


def test_fails_when_the_row_is_older_than_the_test():
    assert_failed(run_wifi(ts_offset_s=-3600))


def test_fails_when_the_row_is_dated_in_the_future():
    assert_failed(run_wifi(ts_offset_s=3600))


def test_fails_when_the_row_time_was_not_synced():
    assert_failed(run_wifi(time_synced=False))


@pytest.mark.parametrize("key,delta", [
    ("pitch_d", 0.01), ("roll_d", -0.01), ("tilt_d", 0.5), ("noise", 0.0001),
    ("temp_c", 1.0), ("stable", True), ("seq", 1), ("boot_count", 1),
])
def test_fails_when_a_row_value_differs_from_the_printed_reading(key, delta):
    assert_failed(run_wifi(value_offsets={key: delta}))


def test_fails_when_the_row_has_another_fw():
    assert_failed(run_wifi(fw_in_row="0.0.9"))


def test_fails_when_the_board_reports_a_failed_upload():
    assert_failed(run_wifi(upload_ok=0))


def test_fails_against_a_silent_port():
    assert_failed(run_wifi(port=FakeSerial()))


def test_fails_when_the_board_does_not_know_the_wifi_command():
    assert_failed(run_wifi(port=StillBoard(FW)))


def test_fails_when_the_board_only_answers_pings():
    assert_failed(run_wifi(port=FakeFirmware(FW)))


# ---- the device-key rules test ----

def test_key_check_passes_when_update_and_delete_are_refused():
    assert run_key_check(FakeSupabase()) is None


def test_key_check_fails_when_an_update_is_accepted():
    assert_failed(run_key_check(FakeSupabase(update_accepted=True)))


def test_key_check_fails_when_an_update_is_applied_without_saying_so():
    assert_failed(run_key_check(FakeSupabase(update_silently_applied=True)))


def test_key_check_fails_when_a_delete_is_accepted():
    assert_failed(run_key_check(FakeSupabase(delete_accepted=True)))


def test_key_check_fails_when_inserts_are_refused():
    db = FakeSupabase()
    db.insert = lambda row: (401, {"message": "new row violates row-level security policy"})

    assert_failed(run_key_check(db))


# ---- secrets.h on the PC ----

EXAMPLE = harness.ROOT / "include" / "secrets.example.h"


def test_secrets_are_read_from_the_firmware_header(tmp_path):
    header = tmp_path / "secrets.h"
    header.write_text(EXAMPLE.read_text(encoding="utf-8")
                      .replace("https://your-project.supabase.co", "https://abc.supabase.co")
                      .replace("your-anon-or-publishable-key", "sb_publishable_xyz"),
                      encoding="utf-8")

    db = supabase_rest.from_secrets(header)

    assert db.base == "https://abc.supabase.co/rest/v1/readings"
    assert db.key == "sb_publishable_xyz"
    assert db.device_id == "node-01"


def test_secrets_still_holding_the_example_values_are_refused(tmp_path):
    header = tmp_path / "secrets.h"
    header.write_text(EXAMPLE.read_text(encoding="utf-8"), encoding="utf-8")

    with pytest.raises(ValueError, match="example"):
        supabase_rest.from_secrets(header)


def test_missing_secrets_are_refused(tmp_path):
    with pytest.raises(ValueError, match="missing"):
        supabase_rest.from_secrets(tmp_path / "secrets.h")

