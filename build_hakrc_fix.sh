#!/bin/bash
set -e
export PATH="/tmp/gcc-arm-none-eabi-10-2020-q4-major/bin:$PATH"
cd /mnt/c/Users/schra/ardupilot
./waf distclean
python3 ./Tools/scripts/build_bootloaders.py HAKRCH743
./waf configure --board HAKRCH743
./waf copter
