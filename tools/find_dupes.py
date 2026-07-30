#!/usr/bin/env python3
"""Find repeated identical code blocks in src/*.c so they can be shared.

Normalises whitespace, drops comments, then hashes every window of N
consecutive statements-worth of lines and reports windows that recur.
Only reports blocks that touch drawing/X11 or the interrupt idiom, which is
what Stage 2 is consolidating.
"""
import re, glob, os, collections, sys
import os, sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import srctree

MINLINES = int(sys.argv[1]) if len(sys.argv) > 1 else 4
INTEREST = re.compile(r'X[A-Z]\w+\s*\(|interrupt_drawing_now|HandlePendingEvents')

def norm_lines(path):
    src = open(path, encoding='utf-8', errors='replace').read()
    src = re.sub(r'/\*.*?\*/', ' ', src, flags=re.S)
    src = re.sub(r'//[^\n]*', '', src)
    out = []
    for i, raw in enumerate(src.split('\n'), 1):
        s = re.sub(r'\s+', ' ', raw).strip()
        if s:
            out.append((i, s))
    return out

blocks = collections.defaultdict(list)
files = srctree.sources(sys.argv[1] if len(sys.argv) > 1 else 'src')
for path in files:
    lines = norm_lines(path)
    for i in range(len(lines) - MINLINES + 1):
        window = lines[i:i + MINLINES]
        text = ' '.join(s for _, s in window)
        if not INTEREST.search(text):
            continue
        blocks[text].append((os.path.basename(path), window[0][0]))

dupes = [(t, locs) for t, locs in blocks.items() if len(locs) >= 3]
dupes.sort(key=lambda kv: -len(kv[1]))

print("repeated %d-line blocks occurring 3+ times (drawing/interrupt only)\n" % MINLINES)
for text, locs in dupes[:12]:
    fl = collections.Counter(f for f, _ in locs)
    print("%3d occurrences across %d files: %s" % (len(locs), len(fl), dict(fl)))
    print("    %s\n" % (text[:150] + ('...' if len(text) > 150 else '')))
