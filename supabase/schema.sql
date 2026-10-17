-- Landslide monitor: Supabase schema (spec #1 "Supabase", spec #14, ticket #19).
-- Run once in the Supabase SQL Editor (see supabase/SETUP.md). Safe to re-run.
--
-- Access rules:
--   * anyone may read (the dashboard uses the same public key);
--   * the device key (anon / publishable) may insert, never update or delete,
--     and only with its device token (#24, `x-device-token`, see below);
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

-- Device token (#24). The public key is in the dashboard for everyone to see,
-- so it alone can't insert: a row is taken only when the request's
-- `x-device-token` header hashes (SHA-256) to the hash stored for the row's
-- device_id. Only hashes are stored, in a schema the API doesn't serve, in a
-- table the anon and authenticated roles have no privilege on. The insert rule
-- reads it through private.device_token_ok(), which runs as its owner.
-- Add or change a device's hash in the SQL Editor (supabase/SETUP.md).
create schema if not exists extensions;
create extension if not exists pgcrypto with schema extensions;  -- digest()

create schema if not exists private;
revoke all on schema private from public, anon, authenticated;
-- The insert rule runs as the caller, so anon must be able to reach the
-- function (USAGE), but gets no privilege on any table in the schema.
grant usage on schema private to anon;

create table if not exists private.device_tokens (
  device_id     text        primary key,
  token_sha256  text        not null check (token_sha256 ~ '^[0-9a-f]{64}$'),  -- lower-case hex
  created_at    timestamptz not null default now()
);

alter table private.device_tokens enable row level security;  -- no policy: nothing via the API
revoke all on private.device_tokens from public, anon, authenticated;

-- True when this request's x-device-token is the one stored for p_device_id.
-- PostgREST puts the request headers (lower-case names) in the
-- `request.headers` setting as JSON; outside an API request it is unset or
-- empty, and the answer is false. Security definer so it can read the hashes;
-- an empty search_path so nothing the caller creates can stand in for them.
create or replace function private.device_token_ok(p_device_id text)
  returns boolean
  language sql
  stable
  security definer
  set search_path = ''
as $$
  select coalesce((
    select t.token_sha256 = encode(extensions.digest(h.token, 'sha256'), 'hex')
    from private.device_tokens t,
         (select nullif(current_setting('request.headers', true), '')::json
                   ->> 'x-device-token' as token) h
    where t.device_id = p_device_id
      and h.token is not null
      and h.token <> ''
  ), false);
$$;

revoke all on function private.device_token_ok(text) from public, anon, authenticated;
grant execute on function private.device_token_ok(text) to anon;

drop policy if exists "device can insert readings" on public.readings;
create policy "device can insert readings"
  on public.readings for insert
  to anon
  with check (private.device_token_ok(device_id));

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
