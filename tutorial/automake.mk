EXTRA_DIST += \
	tutorial/ovn-sandbox \
	tutorial/ovn-setup.sh \
	tutorial/ovn-lb-benchmark.sh \
	tutorial/ovn-lb-benchmark.py \
	tutorial/bgp/Dockerfile \
	tutorial/bgp/setup-l2.sh \
	tutorial/bgp/setup-l2-ovn.sh \
	tutorial/bgp/setup-l3.sh \
	tutorial/bgp/setup-unicast.sh
sandbox: all
	cd $(srcdir)/tutorial && MAKE=$(MAKE) HAVE_OPENSSL=$(HAVE_OPENSSL) \
		./ovn-sandbox -b $(abs_builddir) --ovs-src $(ovs_srcdir) --ovs-build $(ovs_builddir) $(SANDBOXFLAGS)
