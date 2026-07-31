#!/usr/bin/env python3
"""Where the sources and objects are, for the tools that have to find them.

src/ used to be flat, so every harness found its inputs with
glob(srcdir + "/*.o") and they all agreed by accident.  After the tree was
organised into core/ draw/ ui/ apps/ that glob returns nothing, and a harness
that measures nothing reports success -- link_null.py would have linked an
empty object list and announced a clean link.  Six copies of the same glob is
six chances to fix five of them.

rtree/ is excluded everywhere: it is a vendored R-tree library, not Astir
code, and counting it would inflate every number these tools produce.
"""
import os

VENDORED = {"rtree"}


def _walk(srcdir, suffix):
    out = []
    for root, dirs, files in os.walk(srcdir):
        dirs[:] = [d for d in dirs if d not in VENDORED]
        for name in files:
            if name.endswith(suffix):
                out.append(os.path.join(root, name))
    return sorted(out)


def objects(srcdir="src"):
    """Every compiled object belonging to Astir, recursively."""
    return _walk(srcdir, ".o")


def sources(srcdir="src", suffix=".c"):
    """Every source file belonging to Astir, recursively."""
    return _walk(srcdir, suffix)


def layer_of(path):
    """Which architectural layer a file sits in: core, draw, ui, apps.

    Returns the top-level directory under src/, so that a tool can report
    "a core file did X" without knowing the subsystem breakdown.
    """
    parts = os.path.normpath(path).split(os.sep)
    if "src" in parts:
        i = parts.index("src")
        if i + 1 < len(parts):
            return parts[i + 1]
    return "?"


# The Motif front end's entry object.  Several tools use its existence as the
# "has this tree been built?" test, and one uses its symbol table as the
# definition of "what the front end provides".
MAIN_OBJ = os.path.join("ui", "motif", "main.o")


def main_object(srcdir="src"):
    return os.path.join(srcdir, MAIN_OBJ)
