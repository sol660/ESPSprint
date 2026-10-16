"""Upload to Supabase from the real board (#19; spec #14 stories 34-45).

Needs the plugged-in board, a WiFi network from include/secrets.h in range, and
the Supabase project set up (supabase/SETUP.md). The PC reads the Supabase URL
and key from include/secrets.h.

Guards against a false pass (ADR 0001): the row must carry this run's random
tag, be the only such row, be dated after the test started, and hold the
values the board printed.
"""

import math
import time
from datetime import datetime, timezone

import harness

BOOT_WITHIN_S = 5
# Before commands are read, the first wake cycle runs: up to 35 s of reading
# (with no stored baseline) and its upload: up to 2 x 30 s of WiFi, 5 s NTP,
# 10 s HTTPS.
FIRST_REPLY_WITHIN_S = 35 + 75 + 10
# `wifi`: up to 2 x 30 s to join, 5 s NTP, a 10 s reading, 10 s HTTPS, plus margin.
WIFI_WITHIN_S = 60 + 5 + 10 + 10 + 15
# The board's clock (NTP) and the PC's can differ a little.
CLOCK_SKEW_S = 5
# The row holds 4 decimals of what @read prints to 3 (angles) or 1 (temp).
ANGLE_TOLERANCE_DEG = 0.0006
NOISE_TOLERANCE_G = 0.000006
TEMP_TOLERANCE_C = 0.051
SETUP = "check the WiFi networks and Supabase URL/key in include/secrets.h (supabase/SETUP.md)"


def utc_seconds(ts):
    """Supabase's timestamptz text as UTC seconds."""
    return datetime.fromisoformat(ts.replace("Z", "+00:00")).timestamp()


def assert_matches(row, key, printed, tolerance, line):
    value = row.get(key)
    if math.isnan(printed):
        assert value is None, f"{key} is {value} in Supabase, but the board printed nan: {line}"
        return
    assert value is not None and abs(value - printed) <= tolerance, (
        f"{key} is {value} in Supabase, but the board printed {printed}: {line}")


def test_wifi_uploads_exactly_one_row_with_this_runs_tag(board, supabase):
    board.reset()
    board.expect("boot", BOOT_WITHIN_S)
    board.sync(FIRST_REPLY_WITHIN_S)
    started = time.time()
    tag = harness.random_id()

    board.send(f"wifi {tag}")

    read = board.expect("read", WIFI_WITHIN_S, what=f"@read in reply to `wifi {tag}`")
    up = board.expect("upload", WIFI_WITHIN_S, match=lambda t: t.fields.get("tag") == tag,
                      what=f"@upload tag={tag}")
    assert read.fields.get("ok") == "1", f"reading failed: {read.line}"
    assert up.fields.get("ok") == "1", f"upload failed ({SETUP}): {up.line}"

    rows = supabase.rows_with_tag(tag)
    assert len(rows) == 1, (
        f"expected exactly one Supabase row with test_tag={tag}, found {len(rows)}: {rows}")
    row = rows[0]

    assert row.get("time_synced") is True, f"row time not synced (NTP?): {row}"
    ts = utc_seconds(row["ts"])
    assert ts >= started - CLOCK_SKEW_S, (
        f"row ts {row['ts']} is before the test started "
        f"({datetime.fromtimestamp(started, timezone.utc).isoformat()}): an old row? {row}")
    assert ts <= time.time() + CLOCK_SKEW_S, f"row ts {row['ts']} is in the future: {row}"

    for key, printed in (("pitch_d", "pitch"), ("roll_d", "roll"), ("tilt_d", "tilt")):
        assert_matches(row, key, read.number(printed), ANGLE_TOLERANCE_DEG, read.line)
    assert_matches(row, "noise", read.number("noise"), NOISE_TOLERANCE_G, read.line)
    assert_matches(row, "temp_c", read.number("temp"), TEMP_TOLERANCE_C, read.line)
    assert row.get("stable") == (read.fields.get("stable") == "1"), (
        f"stable is {row.get('stable')} in Supabase, the board printed {read.line}")

    assert row.get("device_id") == supabase.device_id, f"wrong device_id: {row}"
    assert row.get("fw") == harness.firmware_version(), f"wrong fw: {row}"
    assert row.get("seq") == int(up.number("seq")) and \
        row.get("boot_count") == int(up.number("boot")), (
        f"row seq/boot_count {row.get('seq')}/{row.get('boot_count')} differ from {up.line}")


def test_device_key_can_insert_but_not_update_or_delete(supabase):
    """Story 43: a leaked device key can't change or delete data. Needs no board."""
    tag = harness.random_id()
    row = {
        "device_id": "hil-key-check", "seq": int(tag), "boot_count": 0,
        "ts": datetime.now(timezone.utc).isoformat(), "time_synced": True,
        "trigger": "scheduled", "pitch_d": 0.0, "roll_d": 0.0, "tilt_d": 0.0,
        "pitch_raw": 0.0, "roll_raw": 0.0, "temp_c": 20.0, "noise": 0.001,
        "stable": True, "level": "OK", "event": "none", "rssi": None,
        "reset_reason": None, "fw": harness.firmware_version(), "batt_v": None,
        "test_tag": tag,
    }
    status, body = supabase.insert(row)
    assert status in (200, 201), f"the device key couldn't insert ({SETUP}): HTTP {status} {body}"
    before = supabase.rows_with_tag(tag)
    assert len(before) == 1, f"expected the inserted row, found {before}"

    status, changed = supabase.update_tagged(tag, {"tilt_d": 99.0, "level": "ALERT"})
    assert not (200 <= status < 300 and changed), (
        f"the device key UPDATED a row (HTTP {status}): fix the rules (supabase/schema.sql): {changed}")
    status, deleted = supabase.delete_tagged(tag)
    assert not (200 <= status < 300 and deleted), (
        f"the device key DELETED a row (HTTP {status}): fix the rules (supabase/schema.sql): {deleted}")

    after = supabase.rows_with_tag(tag)
    assert after == before, f"the row changed or vanished:\nbefore {before}\nafter  {after}"
