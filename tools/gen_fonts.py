#!/usr/bin/env python3
# Copyright (c) 2026 Andrew Yong <me@ndoo.sg>
# SPDX-License-Identifier: MIT
"""
Regenerate LVGL bitmap fonts in src/view/fonts/ from pinned upstream TTF releases.

Mirrors LVGL's own font generator (scripts/built_in_font/built_in_font_gen.py in
lvgl/lvgl): a thin wrapper around `lv_font_conv` with --no-compress --no-prefilter.
Run manually when the source font, size, bpp, or hinting settings change; output is
committed to the repo, not generated at build time.

Requires fontTools (for pinning variable-font instances, e.g. Roboto Medium).
"""

import os
import shutil
import subprocess  # nosec B404
import tarfile
import urllib.request
import zipfile
from urllib.parse import urlparse

from fontTools.ttLib import TTFont
from fontTools.varLib.instancer import instantiateVariableFont

LV_FONT_CONV_VERSION = "1.5.3"
RANGE = "0x20-0x7E"

# Standardized on Roboto: 12px body/default text, 22px large digit labels
# (VFO frequency, zone count). bpp differs per size — 12px reads better at
# 2bpp uncompressed (3bpp's extra antialiasing looked fuzzy at this size),
# 22px at 3bpp (finer antialiasing suits the larger stems better).
# Both use Regular weight (wght=400): the stems hold a solid pixel core
# without the Medium weight bump 10px needed.
#
# Both use --autohint-strong: at 22px, default hinting left an extra stray
# pixel on "6"'s right side; strong autohinting fixes it. It does wipe the
# kerning table (measured — not font- or weight-dependent, it's a property
# of the hint mode itself), which reads worse in running text (e.g.
# "brightness"'s "rig" collapsing together), but is still the better
# trade-off than the stray-pixel artifact it fixes.
FONTS = {
    "roboto": {
        # google/fonts is a monorepo with no per-font releases; pin to a commit.
        "url": "https://raw.githubusercontent.com/google/fonts/"
        "1c627bfa375fc51cf86fabeca4f6e08a95f0aa5c/ofl/roboto/Roboto%5Bwdth%2Cwght%5D.ttf",
        "sizes": [
            (12, 2, "--autohint-strong", {"wght": 400, "wdth": 100}),
            (22, 3, "--autohint-strong", {"wght": 400, "wdth": 100}),
        ],
    },
}

REPO_ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
FONT_CACHE_DIR = os.path.join(REPO_ROOT, "tools", ".font_cache")
OUTPUT_DIR = os.path.join(REPO_ROOT, "src", "view", "fonts")


def _git_config_value(key):
    git_bin = shutil.which("git")
    if not git_bin:
        return None
    try:
        result = subprocess.run(
            [git_bin, "config", key],
            check=True,
            capture_output=True,
            text=True,
            shell=False,  # nosec B603
        )
    except (subprocess.CalledProcessError, FileNotFoundError):
        return None
    value = result.stdout.strip()
    return value or None


def _spdx_header():
    name = _git_config_value("user.name")
    email = _git_config_value("user.email")
    if not name and not email:
        return None

    lines = ["// SPDX-License-Identifier: MIT"]
    if name and email:
        lines.append(f"// SPDX-FileCopyrightText: {name} <{email}>")
    elif name:
        lines.append(f"// SPDX-FileCopyrightText: {name}")
    return "\n".join(lines) + "\n\n"


def _write_font_with_spdx_header(out_path, content):
    header = _spdx_header()
    if not header:
        return
    with open(out_path, "w", encoding="utf-8") as handle:
        handle.write(header + content.lstrip())


def _download(url, dest):
    if os.path.exists(dest):
        return
    parsed = urlparse(url)
    if parsed.scheme not in {"http", "https"}:
        raise ValueError(f"unsupported font URL scheme: {parsed.scheme}")
    print(f"  fetch  {url}")
    urllib.request.urlretrieve(url, dest)  # nosec B310


def _extract_ttf(archive_path, member, dest_path):
    if archive_path.endswith(".zip"):
        with zipfile.ZipFile(archive_path) as zf, zf.open(member) as src, open(
            dest_path, "wb"
        ) as dst:
            shutil.copyfileobj(src, dst)
    else:
        with tarfile.open(archive_path) as tf:
            src = tf.extractfile(member)
            with src, open(dest_path, "wb") as dst:
                shutil.copyfileobj(src, dst)


def fetch_fonts():
    os.makedirs(FONT_CACHE_DIR, exist_ok=True)
    ttf_paths = {}
    for name, info in FONTS.items():
        ttf_path = os.path.join(FONT_CACHE_DIR, f"{name}.ttf")
        if not os.path.exists(ttf_path):
            if "archive" in info:
                archive_path = os.path.join(FONT_CACHE_DIR, info["archive"])
                _download(info["url"], archive_path)
                _extract_ttf(archive_path, info["ttf_in_archive"], ttf_path)
            else:
                _download(info["url"], ttf_path)
        ttf_paths[name] = ttf_path
    return ttf_paths


def instantiate(base_ttf_path, axes):
    """Pin a variable font to a static named-instance TTF, e.g. Roboto Medium."""
    suffix = "-".join(f"{axis}{value}" for axis, value in sorted(axes.items()))
    dest_path = base_ttf_path.removesuffix(".ttf") + f"-{suffix}.ttf"
    if not os.path.exists(dest_path):
        f = TTFont(base_ttf_path)
        instantiateVariableFont(f, axes, inplace=True)
        f.save(dest_path)
    return dest_path


def convert(name, ttf_path, size, bpp, autohint_flag):
    out_name = f"font_{name}_{size}.c"
    out_path = os.path.join(OUTPUT_DIR, out_name)
    lv_font_name = f"font_{name}_{size}"
    cmd = ["npx", "--yes", f"lv_font_conv@{LV_FONT_CONV_VERSION}"]
    if bpp != 3:  # LVGL's format only supports 3bpp when RLE-compressed
        cmd += ["--no-compress", "--no-prefilter"]
    cmd += [
        "--bpp",
        str(bpp),
        "--size",
        str(size),
        "--font",
        ttf_path,
    ]
    if autohint_flag:
        cmd.append(autohint_flag)
    cmd += [
        "-r",
        RANGE,
        "--format",
        "lvgl",
        "--lv-include",
        "lvgl.h",
        "--lv-font-name",
        lv_font_name,
        "-o",
        out_path,
    ]
    print(f"  gen    {out_name}")
    subprocess.run(cmd, check=True, shell=False)  # nosec B603
    with open(out_path, encoding="utf-8") as handle:
        content = handle.read()
    _write_font_with_spdx_header(out_path, content)
    return out_path


def main():
    os.makedirs(OUTPUT_DIR, exist_ok=True)
    print("Fetching source fonts...")
    ttf_paths = fetch_fonts()

    print("Generating LVGL bitmap fonts...")
    generated = []
    for name, base_ttf_path in ttf_paths.items():
        for size, bpp, autohint_flag, axes in FONTS[name]["sizes"]:
            ttf_path = instantiate(base_ttf_path, axes) if axes else base_ttf_path
            generated.append(convert(name, ttf_path, size, bpp, autohint_flag))

    print("\nGenerated:")
    for path in generated:
        print(
            f"  {os.path.relpath(path, REPO_ROOT)}  ({os.path.getsize(path):,} bytes)"
        )


if __name__ == "__main__":
    main()
