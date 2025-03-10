#!/bin/bash

set -ev

# Build and install sparse.
#
# Explicitly disable sparse support for llvm because some travis
# environments claim to have LLVM (llvm-config exists and works) but
# linking against it fails.
# Disabling sqlite support because sindex build fails and we don't
# really need this utility being installed.
#
# Official mirror of the git.kernel.org/pub/scm/devel/sparse/sparse.git.
git clone https://github.com/lucvoo/sparse
cd sparse && make -j4 HAVE_LLVM= HAVE_SQLITE= install && cd ..

# Installing wheel separately because it may be needed to build some
# of the packages during dependency backtracking and pip >= 22.0 will
# abort backtracking on build failures:
#     https://github.com/pypa/pip/issues/10655
pip3 install --disable-pip-version-check --user wheel
pip3 install --disable-pip-version-check --user \
    'flake8==5.0.4' scapy sphinx setuptools pyelftools pyOpenSSL
