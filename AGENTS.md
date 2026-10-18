# Agent guide

This file covers changing this repository. It is public.

The repository builds Zephyr RTOS firmware for the TYT MD-UV390 PLUS 10W GPS
dual-band DMR radio, the simulator that runs the same application on a host
or an emulated Cortex-M4, and the tools around both.

## Requirements

- Verify in the simulator first. `tools/sim.sh` builds the application with
  emulated hardware, and `tools/sim_run.py` runs the scenarios in
  `sim/scenarios/` against the pixel goldens in `sim/golden/`. A change that
  affects the UI, a controller or a model passes all scenarios on both
  targets (`native_sim`, and `--target=arm`) with no unexplained golden or
  heap-baseline difference.
- Hardware is tested in batches that the maintainer schedules. Never flash a
  radio, key the transmitter or write a radio's flash on your own. Read-only
  operations on a connected radio are fine. Transmit and flash-write paths
  stay disabled by default behind explicit gates.
- Keep personal data out of the repository: callsigns, DMR IDs, codeplug
  exports, flash dumps and backups. Test fixtures use invented values.
  `local/` is ignored and holds the real files.
- Keep the application's state in static storage. Do not allocate from the
  heap in controllers and models, and keep call chains on the UI thread short.
- LVGL types and calls appear only under `src/view/`.
  `tools/check_view_layering.sh` enforces this. Controllers and models expose
  data through renderer-neutral callbacks.

## Build

- `pio run -e mduv390plus` builds the firmware. The first run downloads the
  Zephyr framework package (about 1 GB). The platform is pinned
  (`ststm32@19.7.0`, Zephyr 4.4.1) and `tools/apply_patches.py` applies
  `tools/patches/` to it.
- `tools/sim.sh build [--target=arm] [--display=dbi]` builds the simulator in
  Docker (colima on macOS). Run `colima ssh -- sudo fstrim -v
  /mnt/lima-colima` after Docker-heavy work to return the space to the host.

## Change workflow

- Never commit to `main`. Put every change on a branch and open a PR; the
  maintainer lands it with squash-and-merge.
- Never rewrite history or force-push `main`. `--force-with-lease` on a
  feature branch is fine.
- Use a git worktree for a new branch when the main checkout has work in
  progress.
- Put an independent fix in its own, earlier PR. After it lands, rebase the
  dependent PR onto `main`.
- Write the PR title as the squash subject, in the commit subject format
  below.
- When you squash-merge, keep GitHub's default commit title and message:
  `gh pr merge N --squash` without `--subject` or `--body`.
- Before pushing, run:
  - `pio run -e mduv390plus` (prefix with `env -u PLATFORMIO_EXTRA_SCRIPTS`
    when that variable points at a missing file)
  - `tools/check_view_layering.sh`
  - `tools/sim.sh build && python3 tools/sim_run.py`
  - `tools/sim.sh build --target=arm && python3 tools/sim_run.py --target=arm`

## Commits

- Subject in Conventional Commits form, `type(scope): description`, near 50
  characters including the prefix. Then a blank line and a body wrapped at 72
  columns.
- Types: `feat`, `fix`, `perf`, `refactor`, `docs`, `test`, `build`, `ci`,
  `chore`. The scope is optional and names the area (`sim`, `view`, `cps`,
  `tx`). Mark a breaking change with `!` after the scope.
- Start the description in lower case. Write it and every sentence that names
  a change in the imperative mood ("add X", "move Y to Z"), not past or
  present tense. No trailing period.
- Body order: the change, then the motivation, then what is deliberately left
  untouched and why. The last two are optional for small changes. One short
  paragraph each.
- Write the message from `git diff --cached`, not from memory of the plan.
- Describe the code or design change only. No test status, no "pending", no
  process notes.
- Do not put planning labels (milestone or session names) in commit messages
  or code comments.
- Do not name other firmware projects in commits or code comments, except to
  say "compatible with OpenGD77 codeplug" or "compatible with OpenGD77 CPS".
  Describe the behaviour directly.
- Trailers: `Assisted-by: <model name> <noreply@anthropic.com>`, then
  `Signed-off-by` last, built from a fresh `git config user.name` and
  `user.email`. Never use `Co-Authored-By`. Never combine `-s` with
  `--trailer`; pass every trailer with `--trailer`.

## Pull requests

- Use these headings in this order and drop any that do not apply:
  `### Problem` (with `fixes #N`), `### Solution`, `### Regression safety`,
  and `### Build-verified` or `### Bench-tested`.
- Write each paragraph as one unwrapped line.
- In the verification section, state in the past tense what was run and what
  was observed. Name the simulator targets and scenarios run. Say
  "build-verified only" when nothing ran on a radio, with no reason.
- Do not add an AI attribution line.
- Show the user the draft and get approval before posting a PR, comment,
  issue or release note.

## Writing style

- No advertising in commit or PR text: no promotion of a tool or model, no
  "Generated with" lines, no marketing phrasing.
- No editorializing or hedging adjectives ("cleanly", "simply", "robust",
  "elegant", "just works"). If a word can go without changing the meaning,
  remove it. Prefer a plain statement of the mechanism to a coined intensifier
  ("always false", not "hard-false").
- Comment code only for a non-obvious reason or a trap for the next editor.
  One line. Do not narrate what the code does or describe how an external
  system behaves; that belongs in the commit message.
- Keep docs in end-user voice.
- Do not estimate durations in messages ("several minutes").
- A script or CI step that runs longer than about 30 s prints live progress
  every 5 s.

## Simulator and goldens

- Goldens are exact pixel comparisons. Before re-blessing one, open the diff
  image and confirm the change is the intended one; state the reason in the
  commit message. Do not re-bless to make a failure go away.
- Heap baselines (`sim/golden/heap.json`, `heap_arm.json`) record the LVGL
  peak per scenario. Re-bless only for an intended change and say why. The
  ARM numbers are the 32-bit figures; the native ones are inflated by 64-bit
  pointers and are for comparing before and after.
- Scenarios build their codeplug from `tools/testdata/` fixtures, never from
  `local/`.
- The simulator replaces hardware at the devicetree and driver level. Do not
  add simulator conditionals to `src/`.

## Working with the radio

- Detect DFU mode by USB VID:PID `0483:df11`, not by name substrings.
- Flash only through `tools/flash_tyt_dfu.py` (`pio run -e mduv390plus
  --target upload`). The radio must be in DFU mode first: hold PTT and SK1
  while powering on. The bootloader needs its own unlock and XOR sequence;
  `west flash` does not work.
- Before any write to a radio's flash, back up its flash and compare the
  checksum of the backup. Store backups under `local/`. Never commit a backup
  or its contents.
- When the next step needs DFU mode or a power cycle, ask the user to do it.
  Do not leave the radio in an unknown state.
- Transmit is disabled by default. Do not enable the transmit gate, and do not
  run any test that keys the radio, without the user's instruction and a
  dummy load connected.
