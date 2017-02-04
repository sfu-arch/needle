include ../Common.mk

SUITE=perfect
NAME=dwt53
BC=$(BITCODE_REPO)/$(LLVM_VERSION)/$(SUITE)/$(NAME).bc
CFLAGS=
FUNCTION=dwt53_row_transpose
LDFLAGS=
RUNCMD=""
PRERUN="ln -s $(DATA)/perfect-suite-v1.0/suite/pa1/input"
LIBS="-lrt"

include ../Rules.mk
