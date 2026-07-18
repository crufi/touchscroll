#!/usr/bin/env python3
"""Set every folder on an HFS disk image to open in the Finder's list
view ("by Name"), root window included.

Usage: set-folder-views.py <image> [frView-hex]

The classic Finder stores each folder's view mode in the frView field of
the directory's own catalog record (DInfo) -- not in the Desktop DB. The
default value 0x0200 is what System 7.5's Finder itself writes when you
choose "by Name" (not documented in IM:IV or otherwise that I could find,
but calibrated empirically against a real volume: set the
view in the emulator, read the bytes back). Pass a different hex value
to calibrate for another view or Finder version.

Patches the image in place, surgically: only a few bytes inside each
directory record change (view, inited flag, window rect, scroll -- see
the comment at the patch site for why all four), so the catalog B-tree
structure is never touched. Works on a plain HFS image or an Apple-Partition-Map-wrapped
device image (.hda) -- in the normal mac-forks build this runs on the
plain .img before djjr converts it, so the .hda inherits the views.

Stdlib only, on purpose (i.e. we don't use machfs, awesome as it is): 
consumers already need python3 for snow-attach-disk.py, and nobody should 
need pip for a few-byte patch.
"""
import struct
import sys


def find_partition(data):
    if data[0:2] == b'ER':  # Apple Partition Map
        n = 1
        while data[512 * n:512 * n + 2] == b'PM':
            off = 512 * n
            ptype = data[off + 48:off + 80].rstrip(b'\0')
            if ptype == b'Apple_HFS':
                return struct.unpack('>I', data[off + 8:off + 12])[0] * 512
            n += 1
        sys.exit(f'{sys.argv[0]}: no Apple_HFS partition in the partition map')
    if data[1024:1026] == b'BD':
        return 0
    sys.exit(f'{sys.argv[0]}: not an HFS image (no MDB signature, no partition map)')


def main():
    if len(sys.argv) not in (2, 3):
        sys.exit(f'usage: {sys.argv[0]} <image> [frView-hex]')
    path = sys.argv[1]
    frview = int(sys.argv[2], 16) if len(sys.argv) == 3 else 0x0200

    with open(path, 'r+b') as f:
        data = bytearray(f.read())
        base = find_partition(data)
        mdb = data[base + 1024:base + 1024 + 512]
        if mdb[0:2] != b'BD':
            sys.exit(f'{sys.argv[0]}: MDB signature missing')
        (alblksiz,) = struct.unpack('>I', mdb[20:24])
        (alblst,) = struct.unpack('>H', mdb[28:30])

        # The catalog file's up-to-3 extents from the MDB, kept as
        # (absolute file offset, length) so patches land back in place.
        extents = []
        for i in range(3):
            sb, bc = struct.unpack('>HH', mdb[150 + 4 * i:154 + 4 * i])
            if bc:
                extents.append((base + alblst * 512 + sb * alblksiz,
                                bc * alblksiz))

        dirs = {}  # dirID -> (absolute record offset, parent dirID)
        nodesize = 512  # fixed for HFS B-trees
        for ext_off, ext_len in extents:
            for node_off in range(ext_off, ext_off + ext_len, nodesize):
                node = data[node_off:node_off + nodesize]
                if struct.unpack('>b', node[8:9])[0] != -1:  # leaf nodes only
                    continue
                (nrecs,) = struct.unpack('>H', node[10:12])
                for r in range(nrecs):
                    (rec_off,) = struct.unpack(
                        '>H', node[nodesize - 2 * (r + 1):nodesize - 2 * r])
                    keylen = node[rec_off]
                    if keylen == 0:
                        continue
                    doff = rec_off + 1 + keylen
                    if doff % 2:
                        doff += 1
                    if node[doff] != 1:  # directory records only
                        continue
                    # dir record: type(1) resrv(1) flags(2) valence(2)
                    # dirID(4) crDat(4) mdDat(4) bkDat(4), DInfo at +22 (offsets
                    # per IM:IV),
                    # DXInfo at +38. Within DInfo: frRect +0, frFlags +8,
                    # frLocation +10, frView +14. Within DXInfo: frScroll +0.
                    #
                    # frView alone is NOT enough, and neither is adding
                    # kIsInited (0x0100) -- both confirmed empirically by
                    # booting the patched image: windows still opened by
                    # Icon. What a real by-Name window carries, per a full
                    # before/after byte diff of a folder the Finder itself
                    # set and flushed (View menu, then clean Shut Down):
                    #   frRect   = saved window bounds -- an empty rect
                    #              reads as "window never existed", which
                    #              makes the Finder start fresh and ignore
                    #              the rest of the saved state
                    #   frFlags |= 0x0100 (kIsInited)
                    #   frView   = 0x0200 (by Name)
                    #   frScroll = (-8, -16), the list-view scroll origin
                    # The Finder also recorded frOpenChain and a desktop
                    # icon location, but those are incidental open-window
                    # state from the calibration boot -- deliberately not
                    # replicated (a stale open chain would make the Finder
                    # try to reopen windows at startup).
                    # Collect now, patch after -- window rects cascade by
                    # folder depth, and depth needs the full parent map
                    # first. The record's own key carries its parent
                    # folder's dirID at key offset +2.
                    (parid,) = struct.unpack('>I', node[rec_off + 2:rec_off + 6])
                    (dirid,) = struct.unpack('>I', node[doff + 6:doff + 10])
                    dirs[dirid] = (node_off + doff, parid)

        # Depth from the root (dirID 2, whose parent is the pseudo-ID 1):
        # each level's window sits 40px right and down from its parent's,
        # so nested folders open in a tidy staircase instead of a stack.
        # Capped at 5 levels so a pathological tree can't walk the window
        # off a 512x342 screen.
        def depth(dirid):
            d = 0
            while dirid in dirs and dirs[dirid][1] in dirs:
                dirid = dirs[dirid][1]
                d += 1
            return min(d, 5)

        patched = 0
        for dirid, (rec, _) in dirs.items():
            step = 40 * depth(dirid)
            (flags,) = struct.unpack('>H', data[rec + 30:rec + 32])
            struct.pack_into('>4h', data, rec + 22,
                             46 + step, 8 + step, 234 + step, 412 + step)  # frRect
            struct.pack_into('>H', data, rec + 30, flags | 0x0100)         # frFlags
            struct.pack_into('>H', data, rec + 36, frview)                 # frView
            struct.pack_into('>2h', data, rec + 38, -8, -16)               # frScroll
            patched += 1

        f.seek(0)
        f.write(data)
    print(f'set frView=0x{frview:04X} (+kIsInited, cascaded rects) on {patched} folder record(s): {path}')


if __name__ == '__main__':
    main()
