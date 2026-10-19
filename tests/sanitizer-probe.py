"""Require a real ASan finding, not merely a nonzero process exit."""
import os
import subprocess
import sys

environment = dict(os.environ, ASAN_OPTIONS="halt_on_error=1:detect_leaks=0")
result = subprocess.run(sys.argv[1:], capture_output=True, text=True, env=environment, timeout=15)
assert result.returncode != 0 and "ERROR: AddressSanitizer:" in result.stderr, (result.returncode, result.stderr)
assert "zone-memory.c" in result.stderr, result.stderr
print("Sanitizer probe detected:", sys.argv[-1])
