"""Real AllocConsole + stdout/file tee in isolated child processes."""
from pathlib import Path
import subprocess
import sys
import tempfile

exe = str(Path(sys.argv[1]).resolve(strict=True))
debug = sys.argv[2] == "debug"
with tempfile.TemporaryDirectory(prefix="dptdoom-console-") as temporary:
    root = Path(temporary)
    # Match launcher CREATE_NO_WINDOW and shared stdout/stderr log handles.
    with (root / "inherited.log").open("wb") as log:
        result = subprocess.run([exe], cwd=root, stdout=log, stderr=log,
                                creationflags=subprocess.CREATE_NO_WINDOW, timeout=20)
    assert result.returncode == 0
    text = (root / "inherited.log").read_text()
    for token in ("STDOUT-PROBE", "STDERR-PROBE", "relay-line-4999", "END-PROBE"):
        assert token in text, token
    files = list(root.glob("dptdoom-debug-*.log"))
    if debug:
        assert "console=1" in text and "inherited-log=preserved" in text, text[:500]
        assert len(files) == 1
        assert files[0].read_bytes() == (root / "inherited.log").read_bytes()
    else:
        assert "Probe: console=0" in text and not files
    # Pipe output is still usable (startup/error tests use capture_output).
    result = subprocess.run([exe], cwd=root, capture_output=True,
                            creationflags=subprocess.CREATE_NO_WINDOW, timeout=20)
    assert result.returncode == 0 and b"END-PROBE" in result.stdout
    assert b"STDERR-PROBE" in (result.stdout + result.stderr)
print("Console probe:", "Debug AllocConsole + file/inherited tee" if debug else "Release without AllocConsole", "OK")
