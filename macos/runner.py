"""Fixed entry points for the isolated embedded Python interpreter."""
from pathlib import Path
import runpy
import sys

if len(sys.argv) < 2 or sys.argv[1] not in {"config_service", "supervisor"}:
    raise SystemExit("Unsupported ClaudeProtect entry point")
directory = Path(__file__).resolve().parent
entry = sys.argv.pop(1)
sys.path.insert(0, str(directory))
runpy.run_path(str(directory / (entry + ".py")), run_name="__main__")
