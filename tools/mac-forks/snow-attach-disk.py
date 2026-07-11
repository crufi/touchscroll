#!/usr/bin/env python3
"""Copy a Snow (a Mac II-class emulator) workspace (.snoww), adding
one disk image as an additional SCSI target in the first empty
("None") slot.

Snow's CLI can only attach media at launch via --floppy, and that
doesn't work for a plain HFS image the way real floppy hardware would
(confirmed: doesn't boot/mount). Attaching an extra SCSI/HDD-style
device image has no CLI flag at all -- but it's just an edit to the
workspace's own JSON, the same edit Snow itself makes when you attach
a disk through the GUI and save. This automates that, non-interactively.

Touches ONLY the new disk's path -- every other field (ROM, PRAM,
existing disks) is left exactly as the template has it. Confirmed
this matters: the template's other entries are typically bare
relative filenames, and Snow resolves those relative to wherever the
workspace file itself lives on disk -- so the OUTPUT file generally
needs to be written into the same directory as the template (e.g.
Snow's own install directory) for those to keep resolving, while the
newly added disk gets an absolute path since it likely lives elsewhere.

Usage: snow-attach-disk.py <template.snoww> <disk.hda> <output.snoww>
"""
import json
import sys
from pathlib import Path


def main():
    if len(sys.argv) != 4:
        sys.exit(f"usage: {sys.argv[0]} <template.snoww> <disk.hda> <output.snoww>")
    template_path, disk_path, output_path = (Path(p) for p in sys.argv[1:4])

    workspace = json.loads(template_path.read_text())
    targets = workspace.get("scsi_targets", [])

    try:
        slot = targets.index("None")
    except ValueError:
        sys.exit(f"error: no empty SCSI slot in {template_path} (all {len(targets)} are in use)")
    targets[slot] = {"Disk": str(disk_path.resolve())}

    output_path.write_text(json.dumps(workspace, indent=2))
    print(f"attached {disk_path} as SCSI target {slot} -> {output_path}")


if __name__ == "__main__":
    main()
