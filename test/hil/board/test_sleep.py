"""Deep sleep and the command window on the real board (#18; spec #14 stories 29-33).

NEEDS THE esp32dev_hil FIRMWARE: `pio run -e esp32dev_hil -t upload`. It is the
same code as esp32dev with short timings (include/config.h, ESPSPRINT_HIL_TIMINGS),
so a sleep and timer wake take seconds instead of 15 minutes. Against the normal
firmware these tests fail with "no @sleep" (its window is 60 s). Flash esp32dev
again afterwards.

The other board tests (hello, reading) also pass on the esp32dev_hil firmware:
they send a command soon after reset, which keeps the board awake.
"""

import harness

# The esp32dev_hil timings, from include/config.h.
WINDOW_S = 10      # COMMAND_WINDOW_MS
IDLE_S = 20        # SERIAL_SESSION_IDLE_MS
INTERVAL_S = 30    # NORMAL_INTERVAL_S

BOOT_WITHIN_S = 5
# The wake cycle runs before the window opens; with no stored baseline it can
# take up to 35 s (25 s baseline + 10 s reading).
FIRST_CYCLE_S = 35
FIRST_REPLY_WITHIN_S = 45
# A timer wake takes one reading and uploads it, then sleeps. Worst case (user
# approval on #14, 2026-10-16), from include/config.h: the reading, every WiFi
# network timing out, the NTP limit, and the HTTPS connect/handshake plus the
# response wait, with 5 s to spare. 100 s with today's values.
TIMER_WAKE_SLEEPS_WITHIN_S = (
    harness.config_number("READING_TIME_LIMIT_MS") // 1000
    + harness.config_number("WIFI_MAX_NETWORKS") * harness.config_number("WIFI_CONNECT_TIMEOUT_S")
    + harness.config_number("NTP_SYNC_TIMEOUT_MS") // 1000
    + 2 * harness.config_number("HTTP_TIMEOUT_MS") // 1000
    + 5)
# millis() runs off the crystal, so the window and idle times are accurate to
# well under a second; 1 s allows for serial latency.
EARLY_S = 1
# The deep-sleep timer runs off the ESP32's internal RC oscillator, which can be
# off by several percent, and the wake adds boot time: allow 30 s -5/+15 s.
TIMER_EARLY_S = 5
TIMER_LATE_S = 15
FLASH_HIL = "is the esp32dev_hil firmware flashed? (pio run -e esp32dev_hil -t upload)"


def test_sleeps_after_the_window_and_wakes_on_the_timer(board):
    board.reset()
    boot = board.expect("boot", BOOT_WITHIN_S)
    booted = board.now()
    assert boot.fields.get("cause") == "PowerOn", f"a reset should report PowerOn: {boot.line}"

    # No commands: the board listens for the window, then deep-sleeps.
    sleep = board.expect("sleep", FIRST_CYCLE_S + WINDOW_S + 10,
                         what=f"@sleep after the {WINDOW_S} s command window ({FLASH_HIL})")
    asleep = board.now()
    assert asleep - booted >= WINDOW_S - EARLY_S, (
        f"slept {asleep - booted:.1f} s after boot, before the {WINDOW_S} s command window "
        f"had closed: {sleep.line}")
    assert sleep.number("s") == INTERVAL_S, (
        f"sleeping {sleep.fields.get('s')} s, expected the {INTERVAL_S} s interval "
        f"({FLASH_HIL}): {sleep.line}")

    # The timer wakes it, and the boot line says so.
    wake = board.expect("boot", INTERVAL_S + TIMER_LATE_S,
                        what=f"@boot after a {INTERVAL_S} s deep sleep")
    woke = board.now()
    assert wake.fields.get("cause") == "Timer", (
        f"the wake after deep sleep reports cause={wake.fields.get('cause')}, expected Timer "
        f"(PowerOn means the board reset instead of sleeping): {wake.line}")
    assert woke - asleep >= INTERVAL_S - TIMER_EARLY_S, (
        f"woke {woke - asleep:.1f} s after @sleep, expected about {INTERVAL_S} s: {wake.line}")
    # Only cold boots count (the count is kept in flash): a timer wake keeps it.
    assert wake.number("boot") == boot.number("boot"), (
        f"boot count went from {boot.fields.get('boot')} to {wake.fields.get('boot')} "
        f"across a timer wake (counted as a cold boot?): {wake.line}")
    # RTC memory survived the sleep: seq carried on from the readings taken
    # since the reset (it restarts only on a cold boot).
    assert wake.number("seq") > boot.number("seq"), (
        f"seq went from {boot.fields.get('seq')} to {wake.fields.get('seq')} "
        f"(RTC state lost across deep sleep?): {wake.line}")

    # A timer wake doesn't listen: it reads and goes straight back to sleep.
    n = harness.random_id()
    board.send(f"ping {n}")
    board.expect("sleep", TIMER_WAKE_SLEEPS_WITHIN_S,
                 what="@sleep straight after the timer wake")
    assert f"@pong {n}" not in board.recent, (
        "the board answered a command during a timer wake: timer wakes must not listen")


def test_a_command_keeps_the_board_awake_past_the_initial_window(board):
    board.reset()
    board.expect("boot", BOOT_WITHIN_S)
    board.sync(FIRST_REPLY_WITHIN_S)  # a command: stay awake IDLE_S after it

    seen = board.watch(WINDOW_S + 5)  # well past where the initial window would close
    slept = [t.line for t in seen if t.tag == "sleep"]
    assert not slept, f"slept within the window though a command had been sent: {slept}"

    board.sync(BOOT_WITHIN_S)  # still awake and answering
    last_command = board.now()

    sleep = board.expect("sleep", IDLE_S + 5,
                         what=f"@sleep {IDLE_S} s after the last command ({FLASH_HIL})")
    idle = board.now() - last_command
    assert idle >= IDLE_S - EARLY_S, (
        f"slept {idle:.1f} s after the last command, expected {IDLE_S} s: {sleep.line}")
