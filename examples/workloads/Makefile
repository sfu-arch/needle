include Common.mk

TOPTARGETS := clean setup epp-inst epp-run epp-decode needle-path needle-braid needle-run-path needle-run-braid

$(TOPTARGETS): $(APPS)

$(APPS):
	$(MAKE) -C $@ $(MAKECMDGOALS)
		
.PHONY: $(TOPTARGETS) $(APPS)

