EXTRA_DIST += \
	tutorial/ovn-sandbox \
	tutorial/ovn-setup.sh \
	tutorial/ovn-lb-benchmark.sh \
	tutorial/ovn-lb-benchmark.py \
	tutorial/ovn-lsp-install-time.sh \
	tutorial/run-test.sh
sandbox: all
	cd $(srcdir)/tutorial && MAKE=$(MAKE) HAVE_OPENSSL=$(HAVE_OPENSSL) \
		./ovn-sandbox -b $(abs_builddir) --ovs-src $(ovs_srcdir) --ovs-build $(ovs_builddir) $(SANDBOXFLAGS)
