"""A fake Supabase readings table and a fake board whose `wifi` command uploads to it (#19).

Both are built from attributes the self-check sets, so a check can make the
upload path healthy, or broken in one specific way.
"""

import re
import time
from datetime import datetime, timezone

from fake_tilt_firmware import StillBoard


class FakeSupabase:
    """The readings table as the device key sees it. By default the rules are right:
    insert and select work; update and delete are refused with HTTP 401."""

    def __init__(self, device_id="node-01", update_accepted=False, delete_accepted=False,
                 update_silently_applied=False, device_tokens=None, token="fake-device-token"):
        self.device_id = device_id
        self.rows = []
        self.update_accepted = update_accepted    # changes rows and says so
        self.delete_accepted = delete_accepted    # deletes rows and says so
        self.update_silently_applied = update_silently_applied  # changes rows, says nothing
        # Device token (#24): {device_id: token} whose rows need that token in
        # x-device-token; None (the default) checks no token at all, as before #24.
        self.device_tokens = device_tokens
        self.token = token  # what this client sends, like SupabaseRest.token

    def _tagged(self, tag):
        return [r for r in self.rows if r.get("test_tag") == str(tag)]

    def rows_with_tag(self, tag):
        return [dict(r) for r in self._tagged(tag)]

    def insert(self, row):
        return self.insert_with_token(row, self.token)

    def insert_with_token(self, row, token):
        """The rules: with device_tokens set, a row goes in only with its device's token."""
        if self.device_tokens is not None and (
                token is None or self.device_tokens.get(row.get("device_id")) != token):
            return 401, {"code": "42501",
                         "message": 'new row violates row-level security policy for table '
                                    '"readings"'}
        self.rows.append(dict(row))
        return 201, None

    def with_token(self, token):
        """The same table, seen by a client that sends `token` (None: no header)."""
        return _TokenClient(self, token)

    def update_tagged(self, tag, values):
        if self.update_accepted or self.update_silently_applied:
            for r in self._tagged(tag):
                r.update(values)
            return 200, [] if self.update_silently_applied else self.rows_with_tag(tag)
        return 401, {"code": "42501", "message": "permission denied for table readings"}

    def delete_tagged(self, tag):
        if self.delete_accepted:
            gone = self.rows_with_tag(tag)
            self.rows = [r for r in self.rows if r.get("test_tag") != str(tag)]
            return 200, gone
        return 401, {"code": "42501", "message": "permission denied for table readings"}


class _TokenClient:
    """FakeSupabase.with_token(): shares the table, sends its own token."""

    def __init__(self, db, token):
        self.db = db
        self.token = token
        self.device_id = db.device_id

    def insert(self, row):
        return self.db.insert_with_token(row, self.token)

    def rows_with_tag(self, tag):
        return self.db.rows_with_tag(tag)

    def with_token(self, token):
        return _TokenClient(self.db, token)


def iso(seconds):
    return datetime.fromtimestamp(seconds, timezone.utc).isoformat()


class UploadingBoard(StillBoard):
    """A still board whose `wifi <tag>` prints @read and @upload and puts rows in `supabase`.

    Healthy: one row with the tag, dated now, holding the printed values.
    """

    def __init__(self, fw, supabase, rows_uploaded=1, ts_offset_s=0.0, time_synced=True,
                 value_offsets=None, upload_ok=1, row_tag=None, fw_in_row=None, **kw):
        super().__init__(fw, **kw)
        self.supabase = supabase
        self.rows_uploaded = rows_uploaded
        self.ts_offset_s = ts_offset_s
        self.time_synced = time_synced
        self.value_offsets = value_offsets or {}
        self.upload_ok = upload_ok
        self.row_tag = row_tag  # upload the row under this tag instead
        self.fw_in_row = fw_in_row
        self.seq = 1
        self.boot_count = 7

    def _row(self, tag):
        p, r = self._delta()
        row = {
            "device_id": self.supabase.device_id, "seq": self.seq, "boot_count": self.boot_count,
            "ts": iso(time.time() + self.ts_offset_s), "time_synced": self.time_synced,
            "trigger": "scheduled", "pitch_d": round(p, 4), "roll_d": round(r, 4),
            "tilt_d": round((p * p + r * r) ** 0.5, 4), "pitch_raw": self.raw[0],
            "roll_raw": self.raw[1], "temp_c": round(self.temp, 2),
            "noise": round(self.noise, 5), "stable": self.stable == 1, "level": "OK",
            "event": "none", "rssi": -60, "reset_reason": "POWERON",
            "fw": self.fw_in_row or self.fw, "batt_v": None, "test_tag": tag,
        }
        for key, delta in self.value_offsets.items():
            row[key] = (not row[key]) if isinstance(row[key], bool) else row[key] + delta
        return row

    def on_write(self, data):
        super().on_write(data)  # ping, read, baseline, status
        m = re.match(rb"wifi (\S+)\n", data)
        if not m:
            return
        tag = m.group(1).decode()
        self.reads += 1
        self.seq += 1
        self.emit("wifi: connecting, then reading and uploading (up to ~90 s)...")
        self.emit(f"@read ok=1 {self._reading_keys()}")
        for _ in range(self.rows_uploaded):
            self.supabase.insert(self._row(self.row_tag or tag))
        self.emit(f"@upload ok={self.upload_ok} tag={tag} seq={self.seq} boot={self.boot_count}")
