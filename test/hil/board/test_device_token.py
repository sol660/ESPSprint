"""Only the device can insert readings (#24). Needs no board, only the live Supabase project.

The dashboard shows the public key to everyone, so the key alone must not be
enough to insert: Supabase must refuse a row with no `x-device-token` header,
or with a wrong one. The PC reads the URL, key, device id and the right token
from include/secrets.h.

Guards against a false pass (ADR 0001): the refused rows are for the real
device id (so they're refused for the token, not for an unknown device); each
refusal must be the rules' own (HTTP 401/403, code 42501), not some other
error; the refused rows must not be in the table; and the same row with the
right token must go in and be readable with no token at all (as the dashboard
reads).
"""

from datetime import datetime, timezone

import harness

PLACEHOLDER_TOKEN = "your-device-token"  # in secrets.example.h
RLS_REFUSED = "42501"  # PostgreSQL insufficient_privilege: "new row violates row-level security"
SETUP = ("run supabase/schema.sql, store the token's hash and put DEVICE_TOKEN in "
         "include/secrets.h (supabase/SETUP.md)")


def make_row(device_id, tag):
    return {
        "device_id": device_id, "seq": int(tag), "boot_count": 0,
        "ts": datetime.now(timezone.utc).isoformat(), "time_synced": True,
        "trigger": "scheduled", "pitch_d": 0.0, "roll_d": 0.0, "tilt_d": 0.0,
        "pitch_raw": 0.0, "roll_raw": 0.0, "temp_c": 20.0, "noise": 0.001,
        "stable": True, "level": "OK", "event": "none", "rssi": None,
        "reset_reason": None, "fw": harness.firmware_version(), "batt_v": None,
        "test_tag": tag,
    }


def test_insert_without_the_device_token_or_with_a_wrong_one_is_refused(supabase):
    token = supabase.token
    assert token and token != PLACEHOLDER_TOKEN, f"DEVICE_TOKEN isn't set ({SETUP})"
    reader = supabase.with_token(None)  # the dashboard sends no token

    for what, sent in (("no token", None), ("a wrong token", "wrong-" + token)):
        tag = harness.random_id()
        status, body = supabase.with_token(sent).insert(make_row(supabase.device_id, tag))
        assert status in (401, 403) and isinstance(body, dict) and body.get("code") == RLS_REFUSED, (
            f"an insert with {what} wasn't refused by the rules (HTTP {status} {body}): "
            "anyone with the public key could add readings; fix supabase/schema.sql")
        assert reader.rows_with_tag(tag) == [], f"the insert with {what} went in anyway"

    # Control: the same kind of row with the right token goes in, and anyone can read it.
    tag = harness.random_id()
    status, body = supabase.insert(make_row(supabase.device_id, tag))
    assert status in (200, 201), (
        f"the right DEVICE_TOKEN couldn't insert ({SETUP}): HTTP {status} {body}")
    rows = reader.rows_with_tag(tag)
    assert len(rows) == 1 and rows[0].get("device_id") == supabase.device_id, (
        f"expected the row inserted with the right token, readable with no token: {rows}")
