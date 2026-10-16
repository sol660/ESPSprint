-- Landslide monitor: Supabase schema (spec #1 "Supabase", spec #14, ticket #19).
-- Run once in the Supabase SQL Editor (see supabase/SETUP.md). Safe to re-run.
--
-- Access rules:
--   * anyone may read (the dashboard uses the same public key);
--   * the device key (anon / publishable) may insert, never update or delete;
--   * the service_role / secret key and the SQL Editor bypass these rules:
--     never put those on the device.

create table if not exists public.readings (
  id            bigint generated always as identity primary key,
  device_id     text        not null,
  seq           bigint      not null check (seq >= 0),         -- per cold boot, from 1
  boot_count    bigint      not null check (boot_count >= 0), -- cold boots, kept in flash
  ts            timestamptz not null,                        -- UTC, from the device clock
  time_synced   boolean     not null,                        -- false: ts is time since power-on
  trigger       text        not null check (trigger in ('scheduled', 'motion', 'recheck', 'event')),
  pitch_d       real,                                        -- compensated tilt delta, degrees
  roll_d        real,
  tilt_d        real,
  pitch_raw     real,                                        -- averaged angle before compensation
  roll_raw      real,
  temp_c        real,
  noise         real,                                        -- accelerometer std dev, g
  stable        boolean     not null,
  level         text        not null check (level in ('OK', 'WATCH', 'ALERT')),
  event         text        not null default 'none' check (event in ('none', 'shock', 'sensor_error')),
  rssi          smallint,                                    -- dBm
  reset_reason  text,
  fw            text        not null,
  batt_v        real,                                        -- always null in v1
  test_tag      text,                                        -- set on test rows (`wifi` command)
  received_at   timestamptz not null default now(),
  -- Re-sent readings can't create duplicates.
  constraint readings_device_seq_boot_key unique (device_id, seq, boot_count)
);

create index if not exists readings_device_ts_idx on public.readings (device_id, ts desc);
create index if not exists readings_test_tag_idx on public.readings (test_tag) where test_tag is not null;

-- Row level security: only what a policy allows gets through.
alter table public.readings enable row level security;

drop policy if exists "anyone can read readings" on public.readings;
create policy "anyone can read readings"
  on public.readings for select
  to anon, authenticated
  using (true);

drop policy if exists "device can insert readings" on public.readings;
create policy "device can insert readings"
  on public.readings for insert
  to anon
  with check (true);

-- No update or delete policy, and no privilege either: the device key gets an
-- error ("permission denied"), not a silent "0 rows changed".
revoke update, delete, truncate on public.readings from anon, authenticated;
grant select, insert on public.readings to anon;
grant select on public.readings to authenticated;

-- Real data only: what the dashboard and any analysis should read.
create or replace view public.real_readings
  with (security_invoker = true)
  as select * from public.readings where test_tag is null;

grant select on public.real_readings to anon, authenticated;

-- Before real use, delete all test rows (run in the SQL Editor):
--   delete from public.readings where test_tag is not null;
