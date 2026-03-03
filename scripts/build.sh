#!/bin/bash
# Build script for vSG gateway

set -e

SCRIPT_DIR="$( cd "$( dirname "${BASH_SOURCE[0]}" )" && pwd )"
PROJECT_ROOT="$( dirname "$SCRIPT_DIR" )"

BUILD_TYPE="${1:-release}"
BUILD_DIR="${PROJECT_ROOT}/build"

echo "Building vSG Gateway (${BUILD_TYPE})..."

# Create build directory
mkdir -p "$BUILD_DIR"
cd "$BUILD_DIR"

# Configure
echo "Configuring with CMake..."
if [ "$BUILD_TYPE" = "debug" ]; then
    cmake -DCMAKE_BUILD_TYPE=Debug ..
else
    cmake -DCMAKE_BUILD_TYPE=Release -DCMAKE_CXX_FLAGS="-march=native -O3" ..
fi

# Build
echo "Building..."
make -j$(nproc)

echo "Build complete: $BUILD_DIR/vsg_gateway"
