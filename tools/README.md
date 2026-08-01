# Tools

Scripts that are part of building or maintaining Astir, as opposed to the
measurement and migration scaffolding used during the port — that lives on the
machines doing the porting and is deliberately not in the repository, because it
is scaffolding, not product.

Run these from the top of the source tree.

| script | what it does |
|---|---|
| `devdata.sh` | Assembles `artifacts/datadir` so a build in the tree can run without being installed. Symlinks Astir's own `config/`, `symbols/`, `help/` and maps, so editing a rule file takes effect on the next run. `ASTIR_MAPS=<dir>` links a downloaded map collection in as well, and refuses another program's install directory. See [INSTALL.md](../INSTALL.md#running-without-installing). |
| `osm_import.sh` | Fetches an OpenStreetMap extract and converts it into something Astir can read. |
| `symbols_to_vector.py` | Regenerates `src/core/render/symbols_vector.c` — the 211 APRS symbols as outlines — from the original pixel art. The generated file is committed; this is only needed when a symbol changes. |
| `symbol_render_check.c`, `symbol_render_check.sh` | Renders every symbol through the real drawing path and checks the result, so a regenerated table cannot silently produce empty glyphs. |
| `beacon_bench.sh`, `nmea_feed.py` | Drives the beacon schedule from a synthetic GPS and counts what gets keyed, with the interface a pty that reaches no radio. `BEACON_SCENARIO=corner` is the regression: a car that turns once should corner-peg once. `parked` is the field symptom, a stationary receiver whose course wanders. See [below](#the-beacon-bench-runs-on-its-own-session-bus). |
| `trace_norm.py` | Normalises a raw operation trace so two runs of the same build compare equal. Prints what it collapsed and masked, and an operation histogram, on stderr — that output is the coverage report, and is worth reading rather than skipping. |
| `split_file.py`, `split_scope.py` | Move functions between files, and work out where the seam between two halves of a file actually runs. Kept because the split is not finished. |

## The beacon bench runs on its own session bus

`beacon_bench.sh` wraps Astir in `dbus-run-session`, and that is not tidiness.

Astir is a `GtkApplication` with a unique id, so a second copy started while one
is already running registers as a *remote* instance, activates the first one's
window and exits 0 — but only after its core has started and opened the
interfaces. Two things follow, and both cost time before they were understood:

- the bench keys twenty-two bytes and stops in the middle of the `UNPROTO`
  line, which reads exactly like a truncated write to the pty and sends you
  looking at buffering;
- and it is the *running* station whose interfaces got poked, which on a
  machine that transmits is not a harmless mistake.

Uniqueness is arbitrated over the session bus, so its own bus makes it its own
primary instance. Any harness that starts Astir while Astir may already be
running wants the same treatment.

## Two things worth knowing before using the file-splitting scripts

Both of these produced code that compiled, which is why they are recorded here
rather than rediscovered:

- **A brace inside a string or character literal moves every function boundary
  after it.** `db.c` contains `'}'`, the APRS message-acknowledgement character.
  Before literals were blanked along with comments, `split_scope.py` reported one
  102-line function as spanning 16,000 lines — and nothing about the output
  looked wrong, because a big function in a big file is exactly what you expect
  to find.

- **A function can be wrapped in a preprocessor conditional.** `split_file.py`
  once moved a function out and left its `#ifndef` behind: an empty conditional
  in the source file and an unguarded definition in the destination. Both files
  compiled. Check `nm` against the source function list after a move, not just
  that the tree builds.

When a script that edits hundreds of call sites turns out to be wrong, revert
every file it touched and re-run it fixed. Do not patch its output — some of the
corruption it produced will compile.
