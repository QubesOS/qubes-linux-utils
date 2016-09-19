all:
	python setup.py build
.PHONY: all

clean:
	python setup.py clean
.PHONY: clean

install:
	python setup.py install -O1 --skip-build --prefix=/usr --root $(DESTDIR)
#ifeq (1,${DEBIANBUILD})
#	cp *.py $(DESTDIR)/$(PYTHON_SITEARCH)/qubes/
#else
#	cp *.py* $(DESTDIR)/$(PYTHON_SITEARCH)/qubes/
#endif
.PHONY: install
