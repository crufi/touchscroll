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

# The project's name, two ways:
#
# PROJECT names the build artifacts (<project>.img/<project>.hda, the
# Snow workspace, the release zip) and defaults to the repo directory's
# own name. It has to stay a filesystem-safe slug: Make splits
# prerequisite lists on whitespace, so a file target with a space in it
# ("Today's the Day.hda") genuinely cannot work as a Make target --
# don't override this with anything containing spaces.
#
# VOLUME_LABEL is the HFS volume name shown inside the emulator, where
# spaces and apostrophes are fine and pretty ("Today's the Day") --
# set it in your project's Makefile; left unset it falls back to the
# PROJECT slug.
PROJECT      ?= $(notdir $(CURDIR))
VOLUME_LABEL ?= $(PROJECT)

HFS_IMAGE := $(BUILD_DIR)/$(PROJECT).img   # plain HFS, floppy format -- build-floppy.sh's native output

# Every tracked file is a potential input (sidecars for forked files,
# real files directly for mactext ones) -- without depending on them,
# the rule only depended on import.sh itself, so editing a .c file and
# re-running `make run`/`make release` would silently keep serving
# whatever image was already sitting in $(BUILD_DIR), stale content and
# all. But they can't be listed as prerequisites directly: Make splits
# prerequisite lists on whitespace, so one tracked file with a space in
# its name ("TidyMenus cdev.<pi>.rsrc" -- idiomatic classic Mac naming)
# becomes two phantom targets and a hard "No rule to make target"
# error. tracked-stamp.sh collapses the whole set into a single
# space-free stamp file whose mtime advances exactly when any tracked
# file's name/mtime/size changes.
TRACKED_STAMP := $(shell sh tools/mac-forks/tracked-stamp.sh $(BUILD_DIR))

# Label and creator are quoted because a real VOLUME_LABEL has spaces
# and an apostrophe ("Today's the Day") -- and $(strip ...)ed because
# Make keeps the trailing whitespace sitting between a variable's value
# and an inline # comment (`FOO := KAHL   # comment` makes FOO
# "KAHL   "), which unquoted expansion used to hide and quoting would
# otherwise bake into the value.
$(HFS_IMAGE): tools/mac-forks/import.sh $(TRACKED_STAMP)
	sh tools/mac-forks/import.sh
	sh tools/mac-forks/build-floppy.sh $@ $(strip $(VOLUME_BLOCKS)) "$(strip $(VOLUME_LABEL))" "$(strip $(TEXT_CREATOR))"

endif
