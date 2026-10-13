---
name: pio-add-lib
description: Find and add a third-party library (sensor driver, JSON, MQTT, display, etc.) to the PlatformIO project. Use when the user wants to use a library, needs a driver for a component, or a build fails with a missing header.
---

# Add a library

1. Search the PlatformIO registry:
   ```bash
   ~/.platformio/penv/Scripts/pio.exe pkg search "<name or chip>" 2>&1 | head -40
   ```
   Prefer libraries that: support `espressif32` + `arduino`, are actively maintained, and have high popularity. Mention the choice to the user if there are several plausible options.
2. Add it to the shared `[env]` section's `lib_deps` in `platformio.ini` with **owner/name and a caret version pin**, e.g. `bblanchon/ArduinoJson@^7.0.0`. Never leave a library unpinned.
3. Run the `pio-build` skill to download and verify it compiles.
4. If the library's examples show the right include and init pattern, follow them; put any pins it needs in `include/config.h`.

Do not copy library source into `lib/` — `lib/` is for this project's own modules.
