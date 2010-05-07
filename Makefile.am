
SUBDIRS = tokyocabinet src

install:
	cd src && $(MAKE) install

test:
	./run_tests

pkg:
	tools/dist $(PACKAGE_TARNAME)-$(PACKAGE_VERSION)

pkgcheck:
	tools/distcheck $(PACKAGE_TARNAME)-$(PACKAGE_VERSION)

# vim: tabstop=8 shiftwidth=8 noexpandtab
