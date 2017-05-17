PYTHON ?= python

all:
	$(PYTHON) setup.py build
.PHONY: all

clean:
	$(PYTHON) setup.py clean
.PHONY: clean

install:
	$(PYTHON) setup.py install -O1 $(PYTHON_PREFIX_ARG) --root $(DESTDIR)
#ifeq (1,${DEBIANBUILD})
#	cp *.py $(DESTDIR)/$(PYTHON_SITEARCH)/qubes/
#else
#	cp *.py* $(DESTDIR)/$(PYTHON_SITEARCH)/qubes/
#endif
.PHONY: install
