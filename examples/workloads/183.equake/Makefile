include ../Common.mk

SUITE=spec2000
NAME=183.equake
BC=$(BITCODE_REPO)/$(LLVM_VERSION)/$(SUITE)/$(NAME).bc
CFLAGS=
FUNCTION=smvp
LDFLAGS=
RUNCMD="<$(DATA)/$(NAME)/inp-ref.in"
TESTCMD="$(DATA)/$(NAME)/inp-test.in"
PRERUN=

include ../Rules.mk
