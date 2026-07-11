# Generic build/run rules for launching a mac-forks project's source
# in the Snow emulator. Include this from your own project's Makefile:
#
#   SNOW_WORKSPACE ?= $(HOME)/Snow/your-workspace.snoww   # required, no default
#   TEXT_CREATOR   := KAHL                                 # required, no default -- your toolchain's creator code
#   # SNOW_PATH, VOLUME_BLOCKS, VOLUME_LABEL, BUILD_DIR all optional, see below
#
#   include tools/mac-forks/snow.mk
#
# Requires (on top of mac-forks' own requirements): djjr
#   curl -L -o /tmp/djjr.pkg https://diskjockey.onegeekarmy.eu/files/djjr/djjr-2.1.0.pkg && installer -pkg /tmp/djjr.pkg -target CurrentUserHomeDirectory
#   (or: sudo port install djjr)
#
# build-floppy.sh's native output (built via image.mk) is a plain HFS
# image in floppy format -- a real, valid floppy image, just one
# Snow's own --floppy doesn't recognize (confirmed: doesn't
# boot/mount). So it's only an intermediate step here: djjr converts
# it into a partitioned SCSI-style device image (same shape as a real
# hard disk image), which gets attached via the workspace's own
# scsi_targets JSON -- the same edit Snow itself makes when you attach
# a disk through the GUI and save, just automated (snow-attach-disk.py)
# so it can happen from the command line.
#
# The generated workspace has to live in SNOW_PATH itself, not this
# project's BUILD_DIR -- confirmed ("Failed to load workspace: No
# such file or directory"): the template's OTHER entries (ROM, PRAM,
# other disks) are typically bare relative filenames, and Snow
# resolves those relative to wherever the workspace file itself sits.
# Moving the file out of SNOW_PATH breaks every one of those; only
# the newly added disk gets (and needs) an absolute path, since it
# genuinely lives elsewhere.

include tools/mac-forks/image.mk

SNOW_PATH    ?= $(HOME)/Snow
SNOW         := $(SNOW_PATH)/Snow.app/Contents/MacOS/Snow
DEVICE_IMAGE := $(BUILD_DIR)/disk.hda   # disk.img, converted to a SCSI-attachable device

# Named after the including project's own directory, so multiple
# projects using this fragment don't collide writing into SNOW_PATH.
WORKSPACE := $(SNOW_PATH)/$(notdir $(CURDIR)).snoww

.PHONY: all run clean pull guard-hda

# Builds both images, not just disk.img -- disk.hda is what Snow (and a
# release, see release.mk) actually needs, so a plain `make`/`make all`
# should leave it in a working, current state too, not just the
# intermediate. guard-hda listed first (and ahead of $(DEVICE_IMAGE) in
# every target below) so it always runs *before* $(HFS_IMAGE) gets a
# chance to rebuild -- guard-overwrite.sh needs to compare disk.hda
# against the disk.img it was actually converted from, not one that
# just got rebuilt moments before the check ran.
all: guard-hda $(DEVICE_IMAGE)

# guard-overwrite.sh runs before djjr overwrites disk.hda -- if the disk
# looks like it has edits nothing's pulled back yet (see pull-from-disk.sh
# and the target below), this stops here rather than silently discarding
# them. FORCE=1 skips it (needed for non-interactive use, since the
# confirmation prompt reads from stdin).
#
# guard-hda is its OWN phony target, never a recipe line folded into
# $(DEVICE_IMAGE)'s own rule, for two reasons: (1) Make treats disk.hda
# as up to date whenever it's newer than disk.img, which is exactly what
# happens after an in-emulator edit (confirmed: "Nothing to be done for
# `all'", guard never even ran) -- a phony prerequisite has no mtime of
# its own, so listing it as a prerequisite of $(DEVICE_IMAGE) always
# forces that rule's recipe (the djjr conversion) to run too, regardless
# of whether disk.hda already looks newer than disk.img. (2) it
# deliberately does NOT depend on $(HFS_IMAGE) itself, so it never
# triggers disk.img's own rebuild (which runs import.sh, touching local
# files) before the check runs.
#
# It's listed as a prerequisite in TWO places on purpose: first in `all`/
# $(WORKSPACE) (below), so its recipe -- the actual check -- runs before
# $(HFS_IMAGE) gets touched; and again here, on $(DEVICE_IMAGE) itself,
# purely for the forcing effect above (Make won't re-run an
# already-satisfied phony target's recipe twice in one invocation, but
# still treats the target that depends on it as needing a rebuild).
guard-hda:
	FORCE=$(FORCE) sh tools/mac-forks/guard-overwrite.sh $(DEVICE_IMAGE) $(HFS_IMAGE)

$(DEVICE_IMAGE): $(HFS_IMAGE) guard-hda
	djjr convert to-device $(HFS_IMAGE) $@

$(WORKSPACE): guard-hda $(DEVICE_IMAGE) tools/mac-forks/snow-attach-disk.py
	python3 tools/mac-forks/snow-attach-disk.py $(SNOW_WORKSPACE) $(DEVICE_IMAGE) $@

run: $(WORKSPACE)
	$(SNOW) --fullscreen $(WORKSPACE) &

# Pulls files off the *existing* disk.hda back into the working tree --
# for when something got edited directly in the emulator. Deliberately
# does not depend on $(DEVICE_IMAGE) as a prerequisite: that would trigger
# the rebuild rule above first, overwriting the very edits this is meant
# to rescue, before this recipe ever runs.
pull:
	@test -f $(DEVICE_IMAGE) || { echo "no $(DEVICE_IMAGE) -- run 'make run' first" >&2; exit 1; }
	sh tools/mac-forks/pull-from-disk.sh $(DEVICE_IMAGE)

# clean deletes disk.hda outright, same risk as the rebuild above -- same
# guard.
clean: guard-hda
	rm -rf $(BUILD_DIR)
	rm -f $(WORKSPACE)
