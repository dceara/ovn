FROM fedora:41

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
tcpdump \
uuid.x86_64 \
which  \
initscripts \
systemd

# Use systemd as command
CMD [ "/usr/sbin/init" ]
