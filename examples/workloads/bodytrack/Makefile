include ../Common.mk

SUITE=parsec
NAME=bodytrack
BC=$(BITCODE_REPO)/$(LLVM_VERSION)/$(SUITE)/$(NAME).bc
CFLAGS=
FUNCTION=_ZN17ImageMeasurements11InsideErrorERK17ProjectedCylinderRK11BinaryImageRiS6_
LDFLAGS=
RUNCMD="$(DATA)/$(NAME)/sequenceB_4 4 4 4000 5 0 1"
PRERUN=
TESTCMD="$(DATA)/$(NAME)/sequenceB_small 4 1 1000 5 0 1"

include ../Rules.mk
