include ../Common.mk

SUITE=spec2000
NAME=164.gzip
BC=$(BITCODE_REPO)/$(LLVM_VERSION)/$(SUITE)/$(NAME).bc
CFLAGS=
FUNCTION=longest_match
LDFLAGS=
RUNCMD="$(DATA)/$(NAME)/input.combined"
TESTCMD="$(DATA)/$(NAME)/input.compressed"
PRERUN=

include ../Rules.mk
