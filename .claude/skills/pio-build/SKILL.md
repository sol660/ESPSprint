---
name: pio-build
description: Compile the ESP32 firmware with PlatformIO and fix any compiler errors or warnings. Use when the user asks to build, compile, check that code compiles, or after making code changes to src/, include/ or lib/.
---

# Build the firmware

PlatformIO is not on PATH. Always call it by full path:

```bash
PIO=~/.platformio/penv/Scripts/pio.exe
```

## Steps

1. Build the default env (debug):
   ```bash
   ~/.platformio/penv/Scripts/pio.exe run -e esp32dev 2>&1 | tail -40
   ```
   For a release build use `-e esp32dev-release`.
2. If it fails, read the **first** error (later errors are usually cascades), fix it, rebuild. Repeat until clean.
3. Treat new warnings in project code (`src/`, `include/`, `lib/`) as things to fix. Ignore warnings coming from `.pio/` or framework packages.
4. Report the RAM / Flash usage lines from the end of the output. Flag it if Flash > 85% or RAM > 70%.

## Notes

- First build after changing `platform` or `lib_deps` downloads packages and can take several minutes; use a 10-minute timeout.
- `pio run -t clean` if the build behaves strangely after changing build_flags.
- Never edit files under `.pio/` — they are generated.
