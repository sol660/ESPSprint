"""Harness self-check: runs on the PC with no board (and in CI).

It proves the false-positive guards work: a silent or absent board must make
the board suite FAIL, never pass or skip (ADR 0001).
"""

import importlib.util
from pathlib import Path
from types import SimpleNamespace

import pytest

import harness
from fake_serial import FakeFirmware, FakeSerial, FirmwareThatDoesNotReset

HELLO = Path(__file__).resolve().parent.parent / "board" / "test_hello.py"


class FakeClock:
    """Each look at the clock moves it on 50 ms, so timeouts pass instantly."""

    def __init__(self):
        self.t = 0.0

    def __call__(self):
        self.t += 0.05
        return self.t


def run_hello_test(port):
    """Runs the real board hello test against `port`; returns the exception it raised, or None."""
    spec = importlib.util.spec_from_file_location("hello_under_check", HELLO)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    try:
        module.test_hello(harness.Board(port, clock=FakeClock(), sleep=lambda s: None))
    except BaseException as e:  # noqa: BLE001 - a skip must be caught and reported too
        return e
    return None


def assert_failed(outcome):
    assert outcome is not None, "hello test PASSED against a fake port: false-positive guard broken"
    assert isinstance(outcome, (AssertionError, pytest.fail.Exception)), (
        f"hello test must FAIL, but ended with {type(outcome).__name__}: {outcome}")


def test_hello_fails_against_a_silent_port():
    assert_failed(run_hello_test(FakeSerial()))


def test_hello_fails_when_the_only_boot_line_is_stale_input_from_before_the_reset():
    fw = harness.firmware_version()
    stale = FirmwareThatDoesNotReset(fw=fw, stale_lines=[f"@boot fw={fw} boot=7 cause=PowerOn"])

    assert_failed(run_hello_test(stale))


def test_hello_fails_when_the_firmware_version_is_wrong():
    assert_failed(run_hello_test(FakeFirmware(fw="9.9.9-old")))


def test_hello_passes_against_a_board_that_answers_correctly():
    # Positive control: the failures above must come from the guards, not a test that can't pass.
    port = FakeFirmware(fw=harness.firmware_version())

    assert run_hello_test(port) is None
    assert port.written.startswith(b"ping ")


MANUAL_SUITE = """
import pytest

@pytest.mark.manual
def test_tilt_by_hand():
    pass

def test_automatic():
    pass
"""


def test_manual_tests_are_skipped_by_default(pytester):
    pytester.makeconftest((Path(__file__).resolve().parent.parent / "conftest.py").read_text())
    pytester.makefile(".ini", pytest="[pytest]\nmarkers =\n    manual: hand test\n")
    pytester.makepyfile(MANUAL_SUITE)

    pytester.runpytest().assert_outcomes(passed=1, skipped=1)
    pytester.runpytest("--manual").assert_outcomes(passed=2)


def test_firmware_version_is_read_from_config_h():
    assert harness.firmware_version() == "0.1.0"


def usb_port(device, vid=None, pid=None, description="n/a"):
    return SimpleNamespace(device=device, vid=vid, pid=pid, description=description)


CP2102 = dict(vid=0x10C4, pid=0xEA60, description="Silicon Labs CP210x USB to UART Bridge")


def test_tagged_line_gives_tag_and_key_values():
    t = harness.parse_tag("@boot fw=0.1.0 boot=3 cause=PowerOn")

    assert t.tag == "boot"
    assert t.fields == {"fw": "0.1.0", "boot": "3", "cause": "PowerOn"}


def test_bare_values_after_the_tag_are_kept_in_order():
    t = harness.parse_tag("@pong 123456")

    assert t.tag == "pong"
    assert t.args == ["123456"]


def test_untagged_lines_are_ignored():
    assert harness.parse_tag("[  1234][I][main.cpp:42] setup(): hello") is None
    assert harness.parse_tag("tilt delta pitch +0.1 @boot fw=x") is None
    assert harness.parse_tag("") is None


def test_board_port_is_found_by_its_usb_serial_chip():
    ports = [usb_port("COM1", description="Communications Port"), usb_port("COM7", **CP2102)]

    assert harness.find_port(ports, env={}) == "COM7"


def test_esp_port_overrides_the_search():
    ports = [usb_port("COM7", **CP2102)]

    assert harness.find_port(ports, env={"ESP_PORT": "COM9"}) == "COM9"


def test_no_board_fails_and_lists_the_ports_checked():
    ports = [usb_port("COM1", description="Communications Port"),
             usb_port("COM4", vid=0x046D, pid=0xC52B, description="Logitech receiver")]

    with pytest.raises(pytest.fail.Exception) as e:
        harness.find_port(ports, env={})

    assert "COM1" in str(e.value) and "COM4" in str(e.value)


def test_no_ports_at_all_fails_rather_than_skips():
    with pytest.raises(pytest.fail.Exception, match="no serial ports"):
        harness.find_port([], env={})


def test_two_boards_fail_and_ask_for_esp_port():
    ports = [usb_port("COM7", **CP2102), usb_port("COM8", **CP2102)]

    with pytest.raises(pytest.fail.Exception, match="ESP_PORT"):
        harness.find_port(ports, env={})


def test_config_numbers_are_read_from_config_h():
    assert harness.config_number("WIFI_CONNECT_TIMEOUT_S") == 30
    assert harness.config_number("READING_TIME_LIMIT_MS") == 10000  # written `10 * 1000`


def test_config_number_refuses_a_value_set_twice_for_the_hil_build():
    with pytest.raises(AssertionError):
        harness.config_number("NORMAL_INTERVAL_S")  # differs in the esp32dev_hil build


def test_timer_wake_sleep_limit_covers_the_worst_case_upload():
    module = importlib.util.module_from_spec(
        importlib.util.spec_from_file_location("sleep_under_check", HELLO.parent / "test_sleep.py"))
    module.__spec__.loader.exec_module(module)
    # 10 s reading + 2 x 30 s WiFi + 5 s NTP + 2 x 10 s HTTPS + 5 s spare.
    assert module.TIMER_WAKE_SLEEPS_WITHIN_S == 100
