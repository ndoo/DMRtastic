"""
PlatformIO pre-build script: apply tracked framework patches.

Patches in tools/patches/*.patch are applied to the global framework-zephyr
package before each build. Each patch is idempotent — already-applied patches
are skipped cleanly. The script aborts the build if a patch fails to apply.

Usage: automatically invoked via extra_scripts in platformio.ini.
Manual check: python tools/apply_patches.py
"""

import json
import os
import subprocess
import sys

FRAMEWORK_DIR = os.path.expanduser("~/.platformio/packages/framework-zephyr")
EXPECTED_VERSION = "3.40400.260428"


def _get_patches_dir():
    # Prefer PlatformIO's PROJECT_DIR (reliable absolute path); fall back to
    # __file__-relative resolution when running the script directly.
    try:
        Import("env")  # noqa: F821
        return os.path.join(env["PROJECT_DIR"], "tools", "patches")  # noqa: F821
    except NameError:
        pass
    script_dir = os.path.dirname(os.path.abspath(__file__))
    return os.path.join(script_dir, "patches")


def check_version():
    pkg_json = os.path.join(FRAMEWORK_DIR, "package.json")
    try:
        with open(pkg_json) as f:
            version = json.load(f).get("version", "unknown")
    except FileNotFoundError:
        print("ERROR: framework-zephyr not installed. Run: pio run -e mduv390plus")
        sys.exit(1)

    if version != EXPECTED_VERSION:
        print(f"WARNING: framework-zephyr is {version}, patches target {EXPECTED_VERSION}")
        print("         Patches may not apply cleanly.")
        print("         To reset: rm -rf ~/.platformio/packages/framework-zephyr && pio run")


def is_applied(patch_path):
    """
    Check if a patch is already applied.

    For patches with additions: the first added line must be present in the
    target file. For deletion-only patches: the first deleted line must be
    absent from the target file.
    """
    added_sentinel = None
    deleted_sentinel = None
    target_rel = None

    with open(patch_path) as f:
        for line in f:
            if line.startswith("+++ "):
                target_rel = line.split(None, 1)[1].strip()
                if target_rel.startswith("b/"):
                    target_rel = target_rel[2:]
            elif line.startswith("+") and not line.startswith("+++"):
                s = line[1:].strip()
                if s and not added_sentinel:
                    added_sentinel = s
            elif line.startswith("-") and not line.startswith("---"):
                s = line[1:].strip()
                if s and not deleted_sentinel:
                    deleted_sentinel = s

    if not target_rel:
        return False

    target = os.path.join(FRAMEWORK_DIR, target_rel)
    try:
        with open(target, errors="replace") as f:
            content = f.read()
    except FileNotFoundError:
        return False

    if added_sentinel:
        return added_sentinel in content
    if deleted_sentinel:
        return deleted_sentinel not in content
    return False


def apply_patch(patch_path):
    name = os.path.basename(patch_path)

    if is_applied(patch_path):
        print(f"  skip  {name} (already applied)")
        return

    # -l ignores whitespace differences (handles CRLF package files on macOS)
    with open(patch_path) as f:
        r = subprocess.run(
            ["patch", "-d", FRAMEWORK_DIR, "-p1", "--forward", "-l"],
            stdin=f, capture_output=True, text=True,
        )
    if r.returncode == 0:
        print(f"  ok    {name}")
    else:
        print(f"  FAIL  {name}")
        print(r.stdout)
        print(r.stderr, file=sys.stderr)
        sys.exit(1)


def main():
    patches_dir = _get_patches_dir()
    print("Applying framework patches...")
    check_version()
    if not os.path.isdir(patches_dir):
        print("  (no patches directory — skipping)")
        return
    patches = sorted(f for f in os.listdir(patches_dir) if f.endswith(".patch"))
    if not patches:
        print("  (no patches)")
        return
    for patch_file in patches:
        apply_patch(os.path.join(patches_dir, patch_file))
    print("Done.")


# Support both PlatformIO extra_scripts invocation and direct execution.
try:
    Import("env")  # noqa: F821 — injected by PlatformIO
    main()
except NameError:
    pass

if __name__ == "__main__":
    main()
