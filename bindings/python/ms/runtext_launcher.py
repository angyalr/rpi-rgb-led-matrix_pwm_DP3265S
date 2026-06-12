#!/usr/bin/env python3
"""Start the selected display application from the shared INI file."""

import os
import sys
from pathlib import Path

# Add ms directory to path for imports, since WorkingDirectory is project root
sys.path.insert(0, str(Path(__file__).resolve().parent))

from config_store import export_spwm_environment, load_config


SCRIPT_BY_MODE = {
    "ms": Path("bindings/python/ms/runtext.py"),
    "sefag": Path("bindings/python/sefag/runtext.py"),
}


def main():
    project_root = Path(__file__).resolve().parents[3]
    ms_dir = Path(__file__).resolve().parent
    
    # Set PYTHONPATH for the child process so it can find config_store
    env = os.environ.copy()
    existing_path = env.get("PYTHONPATH", "")
    env["PYTHONPATH"] = str(ms_dir) + (":" + existing_path if existing_path else "")
    
    config = load_config()
    export_spwm_environment(config)
    mode = config.get("app", "script", fallback="ms").strip().lower()
    script = SCRIPT_BY_MODE.get(mode)
    if script is None:
        valid = ", ".join(sorted(SCRIPT_BY_MODE))
        raise SystemExit("Unknown app.script '%s'. Valid values: %s" % (mode, valid))

    target = project_root / script
    if not target.exists():
        raise SystemExit("Display script not found: %s" % target)

    os.chdir(str(project_root))
    os.execve(sys.executable, [sys.executable, str(target)], env)


if __name__ == "__main__":
    main()
