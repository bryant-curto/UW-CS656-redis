#!/bin/bash

pushd "$1"
patch -p1 < ../scal.patch
tools/make_deps.sh
build/gyp/gyp --depth=. scal.gyp
V=1 BUILDTYPE=Release make
popd
