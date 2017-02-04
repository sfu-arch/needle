include ../Common.mk

# Variables for this benchmark

SUITE=parsec
NAME=blackscholes
BC=$(BITCODE_REPO)/$(LLVM_VERSION)/$(SUITE)/$(NAME).bc
CFLAGS=
FUNCTION=_Z19BlkSchlsEqEuroNoDivfffffif
LDFLAGS=
RUNCMD=1 input.txt output.txt
PRERUN="ln -s $(DATA)/$(NAME)/input.txt"
TESTCMD="1 in_small.txt output.txt"

include ../Rules.mk
