# Generic "package a release" rule for any mac-forks project. Builds
# a versioned zip containing the project's source disk image -- not a
# compiled binary, since compiling happens by hand inside whatever
# vintage IDE, not something this can automate.
#
#   include tools/mac-forks/release.mk
#
# Then: make release VERSION=v1.2.0
# (VERSION defaults to `git describe` if you don't set it -- falls
# back to the commit hash if the repo has no tags yet)
#
# Tagging the commit is a deliberately separate, plain-git step, not
# folded into this target -- "build an artifact" and "declare this
# commit a release" are different actions, and the latter mutates
# shared repo state (and, if pushed, is visible to others), which
# isn't something a `make` target should do as a side effect. Something
# like:
#   git tag -a v1.2.0 -m "..." && git push origin v1.2.0

include tools/mac-forks/image.mk

# Guard against the `make release VER=...` typo -- VER isn't a variable
# this file reads, so without this it's silently ignored and VERSION
# falls back to `git describe`, producing a validly-built but
# misleadingly-named zip (e.g. a commit-hash name instead of the
# version you meant to tag).
ifeq ($(origin VER),command line)
$(error VER=$(VER) was set, but this uses VERSION -- did you mean 'make release VERSION=$(VER)'?)
endif

VERSION      ?= $(shell git describe --tags --always --dirty)
RELEASE_DIR  ?= dist
RELEASE_NAME := $(notdir $(CURDIR))-$(VERSION)
RELEASE_ZIP  := $(RELEASE_DIR)/$(RELEASE_NAME).zip

.PHONY: release release-clean

release: $(RELEASE_ZIP)

# disk.hda only exists if the including project's Makefile pulled in
# snow.mk (which defines DEVICE_IMAGE) before release.mk -- bundle it
# alongside disk.img when that's the case, since anyone using Snow wants
# the ready-to-attach device image, not just the plain HFS one. Left out
# entirely for projects that never included snow.mk, so `make release`
# doesn't grow a hard djjr dependency for people who don't use Snow.
#
# $(DEVICE_IMAGE)'s own rule (in snow.mk) already lists $(HFS_IMAGE)
# before guard-hda, so it doesn't need repeating here -- Make resolves
# $(HFS_IMAGE) (rebuilding it first if needed) before getting to
# $(DEVICE_IMAGE)'s prerequisites either way.
$(RELEASE_ZIP): $(HFS_IMAGE) $(DEVICE_IMAGE)
	@mkdir -p $(RELEASE_DIR)
	rm -f $@ $(RELEASE_DIR)/$(RELEASE_NAME).img $(if $(DEVICE_IMAGE),$(RELEASE_DIR)/$(RELEASE_NAME).hda)
	cp $(HFS_IMAGE) $(RELEASE_DIR)/$(RELEASE_NAME).img
	$(if $(DEVICE_IMAGE),cp $(DEVICE_IMAGE) $(RELEASE_DIR)/$(RELEASE_NAME).hda)
	cd $(RELEASE_DIR) && zip $(RELEASE_NAME).zip $(RELEASE_NAME).img $(if $(DEVICE_IMAGE),$(RELEASE_NAME).hda)
	rm -f $(RELEASE_DIR)/$(RELEASE_NAME).img $(if $(DEVICE_IMAGE),$(RELEASE_DIR)/$(RELEASE_NAME).hda)

release-clean:
	rm -rf $(RELEASE_DIR)
