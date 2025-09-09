#!/bin/sh

set -ex

gcc -Wall -Wextra -Wpedantic -O3 -march=native make.c -o make_c
./make_c -I/usr/include/luajit-2.1/ -lluajit-5.1 release
