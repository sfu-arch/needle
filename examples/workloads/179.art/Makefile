include ../Common.mk

SUITE=spec2000
NAME=179.art
BC=$(BITCODE_REPO)/$(LLVM_VERSION)/$(SUITE)/$(NAME).bc
CFLAGS=
FUNCTION=match
LDFLAGS=
RUNCMD="-scanfile $(DATA)/$(NAME)/c756hel.in -trainfile1 $(DATA)/$(NAME)/a10.img -trainfile2 $(DATA)/$(NAME)/hc.img -stride 2 -startx 110 -starty 200 -endx 160 -endy 240 -objects 10"
TESTCMD="-scanfile $(DATA)/$(NAME)/c756hel.in -trainfile1 $(DATA)/$(NAME)/a10.img -stride 2 -startx 134 -starty 220 -endx 139 -endy 225 -objects 1"
PRERUN=

include ../Rules.mk

