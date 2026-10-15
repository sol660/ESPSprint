import pytest

import harness

pytest_plugins = ["pytester"]  # the self-check runs small suites of its own


def pytest_addoption(parser):
    parser.addoption("--manual", action="store_true",
                     help="also run tests marked manual (the installer moves the board)")


def pytest_collection_modifyitems(config, items):
    if config.getoption("--manual"):
        return
    skip = pytest.mark.skip(reason="manual test: run with --manual")
    for item in items:
        if "manual" in item.keywords:
            item.add_marker(skip)


@pytest.fixture
def installer(request, pytestconfig):
    """The person at the bench: `installer.do(instructions)` prints them and waits for Enter.

    Only for tests marked manual, so the automatic run never stops for a person.
    Output capture is suspended around each step so the prompt shows and Enter is read.
    """
    if request.node.get_closest_marker("manual") is None:
        pytest.fail("the installer fixture is only for tests marked @pytest.mark.manual")
    capman = pytestconfig.pluginmanager.getplugin("capturemanager")

    def uncaptured(fn):
        def call(text):
            capman.suspend_global_capture(in_=True)
            try:
                return fn(text)
            finally:
                capman.resume_global_capture()
        return call

    return harness.Installer(ask=uncaptured(input), say=uncaptured(print))


@pytest.fixture
def board():
    """The plugged-in board. No board found is a FAIL, never a skip."""
    port = harness.open_port(harness.find_port())
    try:
        yield harness.Board(port)
    finally:
        port.close()
