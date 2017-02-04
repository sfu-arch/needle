include ../Common.mk

SUITE=spec2006
NAME=401.bzip2
BC=$(BITCODE_REPO)/$(LLVM_VERSION)/$(SUITE)/$(NAME).bc
CFLAGS=
FUNCTION=BZ2_compressBlock
LDFLAGS=
RUNCMD="$(DATA)/$(NAME)/input.source 2"
TESTCMD="$(DATA)/$(NAME)/dryer.jpg 2"
PRERUN=

include ../Rules.mk
