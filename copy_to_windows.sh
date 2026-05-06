#!/bin/bash
# Post-build script to copy ESP32-S3 Super Mini binaries to Windows folder
# Usage: ./copy_to_windows.sh (run from /home/ubuntu/ardupilot)

set -e

BUILD_DIR="/home/ubuntu/ardupilot/build/esp32s3supermini/esp-idf_build"
WIN_DIR="/mnt/c/Users/schra/ardupilot/build/esp32s3supermini/esp-idf_build"

echo "=== Copying ESP32-S3 Super Mini binaries to Windows ==="

mkdir -p "$WIN_DIR/bootloader"
mkdir -p "$WIN_DIR/partition_table"

cp "$BUILD_DIR/bootloader/bootloader.bin" "$WIN_DIR/bootloader/"
cp "$BUILD_DIR/partition_table/partition-table.bin" "$WIN_DIR/partition_table/"
cp "$BUILD_DIR/ardupilot.bin" "$WIN_DIR/"

echo "=== Copy complete! ==="
