include ../Common.mk

SUITE=spec2006
NAME=458.sjeng
BC=$(BITCODE_REPO)/$(LLVM_VERSION)/$(SUITE)/$(NAME).bc
CFLAGS=
FUNCTION=gen
LDFLAGS=
RUNCMD="$(DATA)/$(NAME)/ref.txt"
TESTCMD="$(DATA)/$(NAME)/test.txt"
PRERUN=

include ../Rules.mk
