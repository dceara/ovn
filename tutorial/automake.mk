EXTRA_DIST += \
	tutorial/ovn-sandbox \
	tutorial/ovn-setup.sh \
	tutorial/ovn-lb-benchmark.sh \
	tutorial/ovn-lb-benchmark.py \
	tutorial/evpn/Dockerfile \
	tutorial/evpn/setup-l2.sh \
	tutorial/evpn/setup-l2-ovn.sh \
	tutorial/evpn/setup-l3.sh \
	tutorial/evpn/setup-unicast.sh
sandbox: all
	cd $(srcdir)/tutorial && MAKE=$(MAKE) HAVE_OPENSSL=$(HAVE_OPENSSL) \
		./ovn-sandbox -b $(abs_builddir) --ovs-src $(ovs_srcdir) --ovs-build $(ovs_builddir) $(SANDBOXFLAGS)
