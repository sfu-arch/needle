all: needle-run-path 

setup: .setup.success
.setup.success: 
	@echo SETUP
	@rm -rf $(FUNCTION)
	@mkdir $(FUNCTION)
	$(LLVM_OBJ)/llvm-link $(BC) $(BITCODE_REPO)/$(LLVM_VERSION)/lib/m.bc -o $(FUNCTION)/$(NAME).bc
	$(LLVM_OBJ)/opt -O2 -disable-loop-vectorization -disable-slp-vectorization  $(FUNCTION)/$(NAME).bc -o $(FUNCTION)/$(NAME).bc
	@touch .setup.success

epp-inst: .setup.success .epp-inst.success
.epp-inst.success: .setup.success 
	@echo EPP-INST
	cd $(FUNCTION) && \
	export PATH=$(LLVM_OBJ):$(PATH) && \
		$(NEEDLE_OBJ)/epp $(LDFLAGS) -L$(NEEDLE_LIB) -epp-fn=$(FUNCTION) $(NAME).bc -o $(NAME)-epp $(LIBS) 2> ../epp-inst.log
	@touch .epp-inst.success

prerun: .setup.success 
.prerun.success: .setup.success 
ifdef PRERUN
	cd $(FUNCTION) && \
	bash -c $(PRERUN)
endif
	@touch .prerun.success

epp-run: .epp-inst.success .epp-run.success .prerun.success
.epp-run.success: .epp-inst.success .prerun.success
	@echo EPP-RUN
	cd $(FUNCTION) && \
	export LD_LIBRARY_PATH=$(NEEDLE_LIB):/usr/local/lib64 && \
	./$(NAME)-epp $(RUNCMD) 2>&1 > ../epp-run.log
	@touch .epp-run.success

epp-decode: .epp-run.success .epp-decode.success
.epp-decode.success: .epp-run.success 
	@echo EPP-DECODE
	cd $(FUNCTION) && \
	export PATH=$(LLVM_OBJ):$(PATH) && \
    $(NEEDLE_OBJ)/epp -epp-fn=$(FUNCTION) $(NAME).bc -p=path-profile-results.txt 2> ../epp-decode.log
	@touch .epp-decode.success

needle-path: .epp-decode.success .needle-path.success
.needle-path.success: .epp-decode.success 
	@echo NEEDLE-PATH
	cd $(FUNCTION) && \
	export PATH=$(LLVM_OBJ):$(PATH) && \
    python $(ROOT)/examples/scripts/path.py epp-sequences.txt > paths.stats.txt && \
	$(NEEDLE_OBJ)/needle -fn=$(FUNCTION) -ExtractType::path -seq=path-seq-0.txt $(LIBS) -u=$(HELPER_LIB) $(NAME).bc -o $(NAME)-needle-0 2>&1 > ../needle-path.log

needle-braid: .epp-decode.success .needle-braid.success
.needle-braid.success: .epp-decode.success 
	@echo NEEDLE-BRAID
	cd $(FUNCTION) && \
	export PATH=$(LLVM_OBJ):$(PATH) && \
    python $(ROOT)/examples/scripts/braid.py epp-sequences.txt > braids.stats.txt && \
	$(NEEDLE_OBJ)/needle -fn=$(FUNCTION) -ExtractType::braid -seq=braid-seq-0.txt $(LIBS) -u=$(HELPER_LIB) $(NAME).bc -o $(NAME)-needle-0 2>&1 > ../needle-braid.log

needle-run-braid: needle-braid prerun .prerun.success
	cd $(FUNCTION) && \
	export LD_LIBRARY_PATH=$(NEEDLE_LIB):/usr/local/lib64 && \
	./$(NAME)-needle-0 $(RUNCMD) 2>&1 > ../needle-run-braid.log

needle-run-path: needle-path prerun .prerun.success
	cd $(FUNCTION) && \
	export LD_LIBRARY_PATH=$(NEEDLE_LIB):/usr/local/lib64 && \
	./$(NAME)-needle-0 $(RUNCMD) 2>&1 > ../needle-run-path.log

clean:
	@rm -rf $(FUNCTION) *.log .*.success

.PHONY: clean setup
