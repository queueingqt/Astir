#!/usr/bin/env python3
"""Build a tiny PMTiles v3 archive, so the reader can be tested against a file
it did not write.

Written from the specification, independently of src/core/map/pmtiles.c.  A
reader checked only against its own writer agrees with itself about anything.

  ./tools/make_test_pmtiles.py <out.pmtiles>
"""
import gzip, struct, sys

def varint(v):
    out = bytearray()
    while True:
        b = v & 0x7F
        v >>= 7
        out.append(b | (0x80 if v else 0))
        if not v:
            return bytes(out)

def rot(n, x, y, rx, ry):
    if ry == 0:
        if rx == 1:
            x, y = n - 1 - x, n - 1 - y
        x, y = y, x
    return x, y

def tile_id(z, x, y):
    d, s, n = 0, (1 << z) // 2, 1 << z
    while s > 0:
        rx = 1 if (x & s) > 0 else 0
        ry = 1 if (y & s) > 0 else 0
        d += s * s * ((3 * rx) ^ ry)
        x, y = rot(n, x, y, rx, ry)
        s //= 2
    return (4 ** z - 1) // 3 + d

def directory(entries):
    """entries: list of (tile_id, offset, length, run_length), sorted by id."""
    out = bytearray(varint(len(entries)))
    last = 0
    for tid, _, _, _ in entries:
        out += varint(tid - last)
        last = tid
    for _, _, _, run in entries:
        out += varint(run)
    for _, _, ln, _ in entries:
        out += varint(ln)
    prev_off = prev_len = None
    for _, off, ln, _ in entries:
        if prev_off is not None and off == prev_off + prev_len:
            out += varint(0)          # "immediately after the previous entry"
        else:
            out += varint(off + 1)
        prev_off, prev_len = off, ln
    return bytes(out)

def build(path, tiles, tile_compression=2):
    """tiles: {(z,x,y): payload bytes}"""
    blobs, entries, off = bytearray(), [], 0
    for (z, x, y), payload in sorted(tiles.items(), key=lambda kv: tile_id(*kv[0])):
        body = gzip.compress(payload) if tile_compression == 2 else payload
        entries.append((tile_id(z, x, y), off, len(body), 1))
        blobs += body
        off += len(body)

    root = gzip.compress(directory(entries))       # internal compression: gzip
    header_len = 127
    root_off = header_len
    data_off = root_off + len(root)

    zs = [k[0] for k in tiles]
    h = bytearray(b"PMTiles" + bytes([3]))
    h += struct.pack("<QQ", root_off, len(root))   # root dir
    h += struct.pack("<QQ", 0, 0)                  # json metadata
    h += struct.pack("<QQ", 0, 0)                  # leaf dirs
    h += struct.pack("<QQ", data_off, len(blobs))  # tile data
    h += struct.pack("<QQQ", len(tiles), len(tiles), len(tiles))
    h += bytes([1, 2, tile_compression, 1])        # clustered, gzip dirs, tiles, MVT
    h += bytes([min(zs), max(zs)])
    h += struct.pack("<iiii", -1800000000, -850000000, 1800000000, 850000000)
    h += bytes([min(zs)])
    h += struct.pack("<ii", 0, 0)
    assert len(h) == header_len, len(h)

    with open(path, "wb") as f:
        f.write(h); f.write(root); f.write(blobs)
    print("wrote %s: %d tiles, root %d bytes, data %d bytes"
          % (path, len(tiles), len(root), len(blobs)))

if __name__ == "__main__":
    out = sys.argv[1] if len(sys.argv) > 1 else "/tmp/test.pmtiles"
    build(out, {
        (0, 0, 0): b"tile-zero",
        (1, 0, 0): b"tile-one-zero-zero",
        (1, 1, 0): b"tile-one-one-zero",
        (2, 3, 3): b"tile-two-three-three",
    })
