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

Patches the image in place, surgically: only the two frView bytes of
each directory record change, so the catalog B-tree structure is never
touched. Works on a plain HFS image or an Apple-Partition-Map-wrapped
device image (.hda) -- in the normal mac-forks build this runs on the
plain .img before djjr converts it, so the .hda inherits the views.

Stdlib only, on purpose (i.e. we don't use machfs, awesome as it is): 
consumers already need python3 for snow-attach-disk.py, and nobody should 
need pip for a two-byte patch.
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

        patched = 0
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
                    # dirID(4) crDat(4) mdDat(4) bkDat(4) then DInfo;
                    # frView is DInfo+14 (see IM:IV) -> record+36
                    view_off = node_off + doff + 36
                    if struct.unpack('>H', data[view_off:view_off + 2])[0] != frview:
                        struct.pack_into('>H', data, view_off, frview)
                        patched += 1

        f.seek(0)
        f.write(data)
    print(f'set frView=0x{frview:04X} on {patched} folder record(s): {path}')


if __name__ == '__main__':
    main()
