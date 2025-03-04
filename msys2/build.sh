#!/bin/bash

set -ex
mkdir $1
cd $1
../configure
make -j$(nproc) msi
make check || { cat test-suite.log ; exit 1; }
