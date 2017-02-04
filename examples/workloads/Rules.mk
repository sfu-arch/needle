all: needle-run-path 

setup: .setup.success
.setup.success: 
	@rm -rf $(FUNCTION)
	@mkdir $(FUNCTION)
	$(LLVM_OBJ)/llvm-link $(BC) $(BITCODE_REPO)/$(LLVM_VERSION)/lib/m.bc -o $(FUNCTION)/$(NAME).bc
	$(LLVM_OBJ)/opt -O2 -disable-loop-vectorization -disable-slp-vectorization  $(FUNCTION)/$(NAME).bc -o $(FUNCTION)/$(NAME).bc
	@touch .setup.success
	@echo SETUP

epp-inst: setup .epp-inst.success
.epp-inst.success: setup
	cd $(FUNCTION) && \
	export PATH=$(LLVM_OBJ):$(PATH) && \
		$(NEEDLE_OBJ)/epp $(LDFLAGS) -L$(NEEDLE_LIB) -epp-fn=$(FUNCTION) $(NAME).bc -o $(NAME)-epp $(LIBS) 2> ../epp-inst.log
	@touch .epp-inst.success
	@echo EPP-INST

prerun:
ifdef PRERUN
	cd $(FUNCTION) && \
	bash -c $(PRERUN)
endif

epp-run: epp-inst prerun .epp-run.success
.epp-run.success: epp-inst prerun 
	cd $(FUNCTION) && \
	export LD_LIBRARY_PATH=$(NEEDLE_LIB):/usr/local/lib64 && \
	./$(NAME)-epp $(RUNCMD) 2>&1 > ../epp-run.log
	@touch .epp-run.success
	@echo EPP-RUN

epp-decode: epp-run .epp-decode.success
.epp-decode.success: epp-run 
	cd $(FUNCTION) && \
	export PATH=$(LLVM_OBJ):$(PATH) && \
    $(NEEDLE_OBJ)/epp -epp-fn=$(FUNCTION) $(NAME).bc -p=path-profile-results.txt 2> ../epp-decode.log
	@touch .epp-decode.success
	@echo EPP-DECODE

needle-path: epp-decode .needle-path.success
.needle-path.success: epp-decode 
	cd $(FUNCTION) && \
	export PATH=$(LLVM_OBJ):$(PATH) && \
    python $(ROOT)/examples/scripts/path.py epp-sequences.txt > paths.stats.txt && \
	$(NEEDLE_OBJ)/needle -fn=$(FUNCTION) -ExtractType::path -seq=path-seq-0.txt $(LIBS) -u=$(HELPER_LIB) $(NAME).bc -o $(NAME)-needle-0 2>&1 > ../needle-path.log
	@touch .needle-path.success
	@rm -f .needle-braid.success
	@echo NEEDLE-PATH

needle-braid: epp-decode .needle-braid.success
.needle-braid.success: epp-decode 
	cd $(FUNCTION) && \
	export PATH=$(LLVM_OBJ):$(PATH) && \
    python $(ROOT)/examples/scripts/braid.py epp-sequences.txt > braids.stats.txt && \
	$(NEEDLE_OBJ)/needle -fn=$(FUNCTION) -ExtractType::braid -seq=braid-seq-0.txt $(LIBS) -u=$(HELPER_LIB) $(NAME).bc -o $(NAME)-needle-0 2>&1 > ../needle-braid.log
	@touch .needle-braid.success
	@rm -f .needle-path.success
	@echo NEEDLE-BRAID

needle-run-braid: needle-braid prerun
	cd $(FUNCTION) && \
	export LD_LIBRARY_PATH=$(NEEDLE_LIB):/usr/local/lib64 && \
	./$(NAME)-needle-0 $(RUNCMD) 2>&1 > ../needle-run-braid.log

needle-run-path: needle-path prerun
	cd $(FUNCTION) && \
	export LD_LIBRARY_PATH=$(NEEDLE_LIB):/usr/local/lib64 && \
	./$(NAME)-needle-0 $(RUNCMD) 2>&1 > ../needle-run-path.log

clean:
	@rm -rf $(FUNCTION) *.log .*.success

.PHONY: clean setup
