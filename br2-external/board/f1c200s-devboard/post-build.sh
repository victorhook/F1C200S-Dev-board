#!/bin/sh
# Post-build script - runs after rootfs is assembled, before image creation
set -e

TARGET_DIR="$1"

# Ensure init scripts are executable
chmod +x "${TARGET_DIR}/etc/init.d/rcS" 2>/dev/null || true
