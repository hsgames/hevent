default: all

.DEFAULT:
	cd src && $(MAKE) $@

clean:
	cd src && $(MAKE) $@

.PHONY: clean
