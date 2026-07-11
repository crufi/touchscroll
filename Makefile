SNOW_WORKSPACE ?= $(HOME)/Snow/iix.snoww   # point this at your own workspace
TEXT_CREATOR   := KAHL                     # your toolchain's creator code, e.g. KAHL for Symantec/THINK C

include tools/mac-forks/snow.mk
include tools/mac-forks/release.mk
