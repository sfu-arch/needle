include ../Common.mk

SUITE=spec2006
NAME=456.hmmer
BC=$(BITCODE_REPO)/$(LLVM_VERSION)/$(SUITE)/$(NAME).bc
CFLAGS=
FUNCTION=P7Viterbi
LDFLAGS=
RUNCMD="$(DATA)/$(NAME)/nph3.hmm $(DATA)/$(NAME)/swiss41"
TESTCMD=" $(DATA)/$(NAME)/bombesin.hmm"
PRERUN=

include ../Rules.mk
