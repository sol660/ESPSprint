"""Self-check for the manual-test helper (spec #14, story 14): clear instructions, then wait for Enter."""

import sys
from pathlib import Path

import pytest

import harness

HIL = Path(__file__).resolve().parent.parent


def test_each_step_prints_its_instructions_then_waits_for_enter():
    events = []
    installer = harness.Installer(ask=lambda prompt: events.append(("ask", prompt)) or "",
                                  say=lambda text: events.append(("say", text)))

    installer.do("""
        Tilt the board.
        Hold it still.
    """)

    assert [kind for kind, _ in events] == ["say", "ask"]
    assert "Tilt the board.\nHold it still." in events[0][1]
    assert "Enter" in events[1][1]


def test_a_step_fails_rather_than_hangs_when_there_is_no_terminal():
    def closed_stdin(prompt):
        raise EOFError

    installer = harness.Installer(ask=closed_stdin, say=lambda text: None)

    with pytest.raises(pytest.fail.Exception, match="stdin"):
        installer.do("Tilt the board.")


MANUAL_SUITE = """
import pytest

@pytest.mark.manual
def test_by_hand(installer):
    installer.do("Put the board flat on the table.")
    print("AFTER-ENTER")
"""

UNMARKED_SUITE = """
def test_forgot_the_marker(installer):
    installer.do("Put the board flat on the table.")
"""


def run_suite(pytester, monkeypatch, source, *args, stdin):
    """Runs a small suite in a real pytest subprocess, with output capture on (no -s)."""
    pytester.makeconftest((HIL / "conftest.py").read_text())
    pytester.makefile(".ini", pytest="[pytest]\nmarkers =\n    manual: hand test\n")
    pytester.makepyfile(source)
    monkeypatch.setenv("PYTHONPATH", str(HIL))
    return pytester.run(sys.executable, "-m", "pytest", "-p", "no:cacheprovider", "-rA", *args,
                        stdin=stdin, timeout=60)


def test_manual_test_shows_instructions_and_waits_for_enter_despite_output_capture(
        pytester, monkeypatch):
    result = run_suite(pytester, monkeypatch, MANUAL_SUITE, "--manual", stdin=b"\n")

    result.assert_outcomes(passed=1)
    out = result.stdout.str()
    assert "Put the board flat on the table." in out
    assert out.index("Put the board flat on the table.") < out.index("AFTER-ENTER")


def test_manual_test_fails_when_nobody_can_press_enter(pytester, monkeypatch):
    result = run_suite(pytester, monkeypatch, MANUAL_SUITE, "--manual", stdin=b"")

    result.assert_outcomes(failed=1)


def test_installer_is_refused_in_a_test_not_marked_manual(pytester, monkeypatch):
    # Otherwise the normal automatic run would stop and wait for a person.
    result = run_suite(pytester, monkeypatch, UNMARKED_SUITE, stdin=b"\n")

    result.assert_outcomes(errors=1)
    assert "manual" in result.stdout.str()
