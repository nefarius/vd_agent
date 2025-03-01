#!/bin/bash

mkdir $1
cd $1
../configure
make -j$(nproc) msi
