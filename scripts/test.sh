#!/bin/bash
# Test script for vSG gateway

set -e

SCRIPT_DIR="$( cd "$( dirname "${BASH_SOURCE[0]}" )" && pwd )"
PROJECT_ROOT="$( dirname "$SCRIPT_DIR" )"
BUILD_DIR="${PROJECT_ROOT}/build"

echo "Running vSG tests..."

# Ensure build exists
if [ ! -d "$BUILD_DIR" ]; then
    echo "Build directory not found. Building..."
    "$SCRIPT_DIR/build.sh"
fi

cd "$BUILD_DIR"

echo ""
echo "=== Running Metering Tests ==="
if [ -f test_metering ]; then
    ./test_metering
else
    echo "Test binary not found"
    exit 1
fi

echo ""
echo "=== Running Analytics Tests ==="
if [ -f test_analytics ]; then
    ./test_analytics
else
    echo "Test binary not found"
    exit 1
fi

echo ""
echo "=== Running Pipeline Tests ==="
if [ -f test_pipeline ]; then
    ./test_pipeline
else
    echo "Test binary not found"
    exit 1
fi

echo ""
echo "=== All tests passed! ==="
