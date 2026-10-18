#!/usr/bin/env bash
# Copyright (c) 2026 Andrew Yong <me@ndoo.sg>
# SPDX-License-Identifier: MIT
#
# Build and run the native_sim target inside Docker (colima on macOS).
#   tools/sim.sh build [--rebuild]   build the image if needed, then the app
#   tools/sim.sh run [args]          run build/sim/zephyr/zephyr.exe; args go to the binary
#                                    e.g. -script=sim/scenarios/x.txt -lcd-png=sim/out/final.png
#                                    PNGs written under sim/out/ appear on the host there.
#                                    The console (USB CDC stand-in) is a pty inside the container;
#                                    -attach_uart_cmd='<cmd using %s for the pty path>' attaches a client.
#                                    The codeplug flash is a fresh copy of
#                                    local/radio_stock_backup.bin (or $SIM_CODEPLUG) at
#                                    sim/out/codeplug.bin; the original is never opened.
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
IMAGE=dmrtastic-sim
VOLUME=dmrtastic-sim-ws
BOARD=native_sim/native/64

if command -v colima >/dev/null && ! colima status >/dev/null 2>&1; then
    colima start
fi

dock() {
    docker run --rm -v "$VOLUME:/ws" -v "$ROOT:/ws/app" "$@"
}

cmd_build() {
    if [[ "${1:-}" == "--rebuild" ]] || ! docker image inspect "$IMAGE" >/dev/null 2>&1; then
        docker build -f "$ROOT/Dockerfile.sim" -t "$IMAGE" "$ROOT"
    fi
    dock "$IMAGE" bash -euo pipefail -c '
        cd /ws
        [[ -d .west ]] || west init -l --mf sim/west.yml app
        west config manifest.file sim/west.yml
        west update --narrow --fetch-opt=--depth=1
        pip install --no-cache-dir -q -r zephyr/scripts/requirements-base.txt

        for p in app/tools/patches/*.patch; do
            if patch -d zephyr -p1 --forward -l --dry-run <"$p" >/dev/null 2>&1; then
                patch -d zephyr -p1 --forward -l <"$p" >/dev/null
                echo "  ok    $(basename "$p")"
            else
                echo "  skip  $(basename "$p")"
            fi
        done

        west build -b '"$BOARD"' -d app/build/sim app -- \
            -DCONF_FILE="/ws/app/zephyr/prj.conf;/ws/app/sim/prj_sim.conf" \
            -DZEPHYR_EXTRA_MODULES=/ws/app/sim -DDTS_ROOT=/ws/app/zephyr \
            -DEXTRA_DTC_OVERLAY_FILE=/ws/app/sim/boards/native_sim_native_64.overlay
    '
    echo "Docker-heavy run: consider 'colima ssh -- sudo fstrim -v /mnt/lima-colima'"
}

cmd_run() {
    mkdir -p "$ROOT/sim/out"
    local stock="${SIM_CODEPLUG:-}" main_root args=("$@")
    if [[ -z "$stock" ]]; then
        main_root="$(git -C "$ROOT" worktree list --porcelain | sed -n '1s/^worktree //p')"
        for stock in "$ROOT/local/radio_stock_backup.bin" "$main_root/local/radio_stock_backup.bin"; do
            [[ -f "$stock" ]] && break
        done
    fi
    if [[ -f "$stock" ]]; then
        cp "$stock" "$ROOT/sim/out/codeplug.bin"
        args+=(-codeplug=sim/out/codeplug.bin)
    fi
    set -- "${args[@]}"
    dock -t -w /ws/app "$IMAGE" build/sim/zephyr/zephyr.exe "$@"
}

case "${1:-}" in
    build) shift; cmd_build "$@" ;;
    run) shift; cmd_run "$@" ;;
    *) echo "usage: $0 build [--rebuild] | run [args]" >&2; exit 2 ;;
esac
