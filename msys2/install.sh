#!/bin/bash

a=()
for e in i686 ucrt-x86_64; do
    for p in libpng msitools toolchain zlib; do
        a+=(mingw-w64-$e-$p)
    done
done

exec pacman --noconfirm -S autotools ${a[@]}
