"""Run a test in a temporary working directory, forwarding its exit status."""
import subprocess
import sys
import tempfile

with tempfile.TemporaryDirectory(prefix="dptdoom-test-") as directory:
    sys.exit(subprocess.run(sys.argv[1:], cwd=directory).returncode)
