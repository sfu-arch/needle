include ../Common.mk

SUITE=spec2006
NAME=444.namd
BC=$(BITCODE_REPO)/$(LLVM_VERSION)/$(SUITE)/$(NAME).bc
CFLAGS=
FUNCTION=_ZN20ComputeNonbondedUtil26calc_pair_energy_fullelectEP9nonbonded
LDFLAGS=
RUNCMD="--input $(DATA)/$(NAME)/namd.input --iterations 20 --output namd.out"
TESTCMD="--input $(DATA)/$(NAME)/namd.input --iterations 10 --output namd.out"
PRERUN=

include ../Rules.mk
