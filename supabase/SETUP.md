# Supabase setup (first time)

This sets up the database the board uploads its readings to. It takes about 10 minutes, and you don't need any Supabase experience. The table and its access rules are in [`schema.sql`](schema.sql).

## 1. Create the project

1. Go to <https://supabase.com>, sign up (GitHub login works), and click **New project**.
2. Pick any name (e.g. `landslide-monitor`), set a database password (save it somewhere; the board doesn't need it), and choose the region nearest to you.
3. Wait a minute or two until the project dashboard shows it's ready.

## 2. Create the readings table

1. In the left sidebar, open **SQL Editor** and click **New query**.
2. Paste the whole of [`supabase/schema.sql`](schema.sql) and click **Run**. It should say "Success. No rows returned".
3. Check: in **Table Editor**, a `readings` table now exists (empty). Its padlock shows that row level security (RLS) is on.

Running the file again is safe, so if you change it later, run it again.

## 3. Copy the URL and key into `include/secrets.h`

1. If `include/secrets.h` doesn't exist yet, copy `include/secrets.example.h` to `include/secrets.h`. Git ignores this file, so your secrets stay off GitHub.
2. In Supabase, open **Project Settings → API** (in newer dashboards: **Project Settings → Data API** for the URL and **API Keys** for the key).
3. Copy the **Project URL** (`https://<something>.supabase.co`) into `SUPABASE_URL`.
4. Copy the **anon public** key (a long `eyJ...`) or the **publishable** key (`sb_publishable_...`) into `SUPABASE_KEY`.
   - **Never** use the `service_role` or secret key. It ignores the access rules and could delete everything.
5. While you're in the file, fill in your two WiFi networks (home first, then school), `DEVICE_ID` and `NTFY_TOPIC`.

Keep each value on one line, as in the example. The PC board tests read `SUPABASE_URL`, `SUPABASE_KEY` and `DEVICE_ID` from this file.

## 4. Check it works

1. Flash the firmware, open the serial monitor, press EN, and type `wifi` within 60 s.
2. You should see `@read ...` and then `@upload ok=1 tag=<number> ...`.
3. In **Table Editor → readings** there is a new row with that number in `test_tag`.
4. To run the automatic check from the PC instead, close the serial monitor and run `.venv/Scripts/python.exe -m pytest test/hil/board/test_upload.py` from the repo root. It also checks that the device key can't change or delete rows. (Run `~/.platformio/penv/Scripts/python.exe scripts/setup_venv.py` once first if `.venv` doesn't exist.)

## Test rows

Rows uploaded by `wifi` have a `test_tag`. The `real_readings` view leaves them out, so use that view for real data. The device key can't delete anything, so test rows build up. Before real use, delete them all in the **SQL Editor**:

```sql
delete from public.readings where test_tag is not null;
```

## If something goes wrong

Watch the serial monitor while you type `wifi`. The lines starting with `[E]` or `[W]` say what failed.

| What you see | What to check |
|---|---|
| `WiFi: <name> not joined within 30 s`, for both networks | The SSID and password in `WIFI_NETWORKS` (both are case-sensitive). The ESP32 only joins 2.4 GHz networks, not 5 GHz ones. |
| `NTP sync timed out` | The network blocks internet time (some school networks do). The reading still uploads, but with `time_synced=false`, so the board upload test fails. Try another network, such as a phone hotspot set up as the home network. |
| `Supabase insert failed: HTTP 401` with "Invalid API key" | The key in `SUPABASE_KEY` is wrong or cut off. Copy it again from the dashboard. |
| `Supabase insert failed: HTTP 401` or `403` with `42501` (permission denied) | The access rules are missing. Run `schema.sql` again. |
| `Supabase insert failed: HTTP 404`, or a negative number such as `-1` | `SUPABASE_URL` is wrong (it must start with `https://`), or `schema.sql` hasn't been run yet. |
| `Supabase insert failed: HTTP 400` | The table doesn't match the firmware (the message names the column). Run `schema.sql` again. |
| `@upload ok=1`, but no row in the Table Editor | Click the refresh button in the Table Editor, and make sure you're looking at the `readings` table in the right project. |

A free Supabase project is paused after about a week with no activity. If uploads suddenly fail with an error that isn't listed above, open the dashboard and click **Restore project**.
