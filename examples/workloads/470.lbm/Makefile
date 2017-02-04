include ../Common.mk

SUITE=spec2006
NAME=470.lbm
BC=$(BITCODE_REPO)/$(LLVM_VERSION)/$(SUITE)/$(NAME).bc
CFLAGS=
FUNCTION=LBM_performStreamCollide
LDFLAGS=
RUNCMD="1000 reference.dat 0 0 $(DATA)/$(NAME)/100_100_130_ldc.of"
TESTCMD="300 reference.dat 0 1  $(DATA)/$(NAME)/test/100_100_130_cf_a.of"
PRERUN=

include ../Rules.mk
