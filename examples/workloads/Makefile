include Common.mk

TOPTARGETS := clean setup epp-inst epp-run epp-decode 

$(TOPTARGETS): $(APPS)

$(APPS):
	$(MAKE) -C $@ $(MAKECMDGOALS)
		
.PHONY: $(TOPTARGETS) $(APPS)

