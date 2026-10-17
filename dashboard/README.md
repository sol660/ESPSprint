# Dashboard

One static page that shows the slope's status on a phone: the current level, tilt and temperature charts with the WATCH and ALERT lines, an events list and an OFFLINE badge. It reads the `real_readings` view in Supabase with the public key, so there's no login and no server code. Photos come later (#10).

## Try it without Supabase

The page uses JavaScript modules, so it has to be served over HTTP (opening the file directly doesn't work). From the repo root:

```sh
~/.platformio/penv/Scripts/python.exe -m http.server 8000 --directory dashboard
```

Then open <http://localhost:8000/?demo> for made-up data, or <http://localhost:8000/?demo=offline> to see the OFFLINE badge.

## Fill in `config.js`

1. Copy `dashboard/config.example.js` to `dashboard/config.js`. Git ignores `config.js`.
2. Set `supabaseUrl`, `supabaseKey` and `deviceId` to the same values as `SUPABASE_URL`, `SUPABASE_KEY` and `DEVICE_ID` in `include/secrets.h` (see `supabase/SETUP.md` for where they come from).
   - The key must be the **anon** (`eyJ...`) or **publishable** (`sb_publishable_...`) key. Everything in `config.js` is visible to anyone who opens the page. Never use the `service_role` or secret key.
3. Leave `watchDeg` and `alertDeg` alone unless you change `WATCH_DEG` or `ALERT_DEG` in `include/config.h`. If you do, change them in `config.example.js` (and your `config.js`) too: the dashboard tests fail while they differ.
4. Open <http://localhost:8000/> (server as above). It should show the newest real readings.

## Publish on GitHub Pages

The workflow `.github/workflows/dashboard-pages.yml` publishes the page. It only runs when you start it, and it needs this one-time setup first:

1. On GitHub, open the repo's **Settings → Pages** and set **Source** to **GitHub Actions**.
   - This repo is private, and GitHub Pages on a private repo needs GitHub Pro (free for students with the GitHub Student Developer Pack). Otherwise the repo has to be made public first. Even a site from a private repo is public to anyone with the link.
   - The page only holds the public key, which the database rules already let anyone use to read readings (and insert them). That is the "public read access" the spec asks for.
2. **Settings → Secrets and variables → Actions**:
   - **Variables** tab: add `SUPABASE_URL` and `DEVICE_ID`.
   - **Secrets** tab: add `SUPABASE_PUBLIC_KEY` (the anon or publishable key). The workflow refuses a secret or `service_role` key.
3. **Actions → Dashboard to GitHub Pages → Run workflow**. When it finishes, the page is at `https://<user>.github.io/<repo>/` (the run shows the link).

Run it again after changing the dashboard or the values.

## Add it to the home screen

- **iPhone (Safari):** open the link, tap **Share**, then **Add to Home Screen**.
- **Android (Chrome):** open the link, tap the **⋮** menu, then **Add to Home screen** (or **Install app**).

It then opens full screen like an app. It reloads the data every 5 minutes and whenever you switch back to it.

## What it shows

- **Status card:** the newest reading's level, time (in the phone's time zone), tilt delta (combined, and pitch / roll) and temperature.
- **OFFLINE badge:** when the newest reading is more than 2 h old. The device uploads about once an hour, so one missed upload doesn't trigger it.
- **Tilt chart:** pitch, roll and combined tilt delta, with dashed WATCH and ALERT lines. The levels are applied to the combined value.
- **Temperature chart.**
- **Events:** level changes (worked out by comparing each reading with the one before), shocks and sensor errors, newest first.

It loads the newest 1000 readings (about 10 days at one every 15 minutes). Readings taken before the device's clock was first set are left out.

## Tests

`node --test dashboard/test/*.test.js` from the repo root (Node 20 or later, nothing to install). CI runs them too. They cover the level/event logic, the OFFLINE rule, the chart data, the threshold values against `include/config.h`, and the deploy's key check.
