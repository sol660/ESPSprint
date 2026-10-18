"""Create the project venv (.venv) for the board tests and install test/hil/requirements.txt.

Run it with PlatformIO's Python, so a PlatformIO update can't break the test tools:

    ~/.platformio/penv/Scripts/python.exe scripts/setup_venv.py      (Windows)
    python3 scripts/setup_venv.py                                    (CI / Linux)

Safe to re-run: it reuses an existing .venv and only installs what's missing.
"""

import subprocess
import sys
import venv
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
VENV = ROOT / ".venv"
REQUIREMENTS = ROOT / "test" / "hil" / "requirements.txt"


def venv_python() -> Path:
    if sys.platform == "win32":
        return VENV / "Scripts" / "python.exe"
    return VENV / "bin" / "python"


def main() -> int:
    if not venv_python().exists():
        print(f"creating {VENV} from {sys.executable}")
        venv.EnvBuilder(with_pip=True).create(VENV)
    py = str(venv_python())
    subprocess.check_call([py, "-m", "pip", "install", "--disable-pip-version-check", "-q",
                           "-r", str(REQUIREMENTS)])
    print(f"ready: {py} -m pytest test/hil/board")
    return 0


if __name__ == "__main__":
    sys.exit(main())
