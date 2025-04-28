FROM fedora:latest

RUN dnf install -y --skip-broken \
autoconf \
automake \
conntrack-tools \
dhcp-client \
dnf-utils \
file \
fping \
frr \
gcc \
gettext-devel \
git \
glibc-langpack-en \
hostname \
iproute \
iputils \
iptables \
js-d3-flame-graph \
libcap-devel \
libreswan \
libtool \
libxslt \
lksctp-tools-devel \
make \
meson \
net-tools.x86_64 \
ninja-build \
nmap \
openssh-clients \
openssh-server \
openssl \
openssl-devel \
perf \
procps-ng \
python3 \
python3-devel \
python3-pip \
python3-psutil \
python3-six \
resource-agents \
rpmbuild \
tcpdump \
uuid.x86_64 \
which  \
initscripts \
systemd

ARG OVN_PATH

RUN git clone https://github.com/ovn-org/ovn --single-branch --revision v25.03.0 --depth 1

WORKDIR /ovn
RUN git submodule update --init --depth 1

WORKDIR /ovn/ovs
RUN sed -e 's/@VERSION@/0.0.1/' rhel/openvswitch-fedora.spec.in > /tmp/ovs.spec
RUN dnf builddep -y /tmp/ovs.spec
RUN ./boot.sh
RUN ./configure -v
RUN make rpm-fedora
RUN rm rpm/rpmbuild/RPMS/x86_64/*debug*
RUN rm rpm/rpmbuild/RPMS/x86_64/*devel*

WORKDIR /ovn
RUN sed -e 's/@VERSION@/0.0.1/' rhel/ovn-fedora.spec.in > /tmp/ovn.spec
RUN dnf builddep -y /tmp/ovn.spec
RUN ./boot.sh
RUN ./configure
RUN make rpm-fedora
RUN rm rpm/rpmbuild/RPMS/x86_64/*docker*

RUN find /ovn/ -name *.rpm
RUN dnf install -y /ovn/ovs/rpm/rpmbuild/RPMS/noarch/*.rpm /ovn/ovs/rpm/rpmbuild/RPMS/x86_64/*.rpm
RUN dnf install -y /ovn/rpm/rpmbuild/RPMS/x86_64/*.rpm

# Use systemd as command
CMD [ "/usr/sbin/init" ]
