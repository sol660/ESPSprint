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
def board():
    """The plugged-in board. No board found is a FAIL, never a skip."""
    port = harness.open_port(harness.find_port())
    try:
        yield harness.Board(port)
    finally:
        port.close()
