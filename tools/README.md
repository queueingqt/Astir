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
| `trace_norm.py` | Normalises a raw operation trace so two runs of the same build compare equal. Prints what it collapsed and masked, and an operation histogram, on stderr — that output is the coverage report, and is worth reading rather than skipping. |
| `split_file.py`, `split_scope.py` | Move functions between files, and work out where the seam between two halves of a file actually runs. Kept because the split is not finished. |

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
