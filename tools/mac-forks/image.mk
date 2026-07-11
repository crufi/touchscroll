# Shared rule for building a mac-forks project's source disk image.
# Both snow.mk (to launch it in an emulator) and release.mk (to
# package it) need this same rule -- factored out here, rather than
# each defining it independently, since a project including both
# would otherwise get a "overriding recipe for target" warning from
# Make for the second one. Include-guarded so it's safe to `include`
# from more than one place.
ifndef MAC_FORKS_IMAGE_MK
MAC_FORKS_IMAGE_MK := 1

BUILD_DIR     ?= build
VOLUME_BLOCKS ?= 8192   # 512-byte blocks = 4MB; bump if the project outgrows it
VOLUME_LABEL  ?= Source

HFS_IMAGE := $(BUILD_DIR)/disk.img   # plain HFS, floppy format -- build-floppy.sh's native output

# Every tracked file is a potential input (sidecars for forked files,
# real files directly for mactext ones) -- without this, the rule only
# depended on import.sh itself, so editing a .c file and re-running
# `make run`/`make release` would silently keep serving whatever image
# was already sitting in $(BUILD_DIR), stale content and all. Confirmed:
# this is exactly how an already-fixed mactext encoding issue kept
# showing up in Snow long after the source was actually correct.
TRACKED_FILES := $(shell git -c core.quotePath=false ls-files)

$(HFS_IMAGE): tools/mac-forks/import.sh $(TRACKED_FILES)
	sh tools/mac-forks/import.sh
	sh tools/mac-forks/build-floppy.sh $@ $(VOLUME_BLOCKS) $(VOLUME_LABEL) $(TEXT_CREATOR)

endif
