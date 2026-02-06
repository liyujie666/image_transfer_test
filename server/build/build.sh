#!/bin/bash
rm -rf CMakeCache.txt CMakeFiles Makefile cmake_install.cmake
cmake .. -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX=install_root && make -j$(nproc)
# make install
