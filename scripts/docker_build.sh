#!/bin/bash
# Docker build script for vSG gateway

set -e

SCRIPT_DIR="$( cd "$( dirname "${BASH_SOURCE[0]}" )" && pwd )"
PROJECT_ROOT="$( dirname "$SCRIPT_DIR" )"

TAG="${1:-vsg:latest}"

echo "Building Docker image: $TAG"

docker build \
    -f "$PROJECT_ROOT/docker/Dockerfile" \
    -t "$TAG" \
    "$PROJECT_ROOT"

echo "Docker image built successfully: $TAG"
echo ""
echo "Run with:"
echo "  docker run --privileged --network host -v /dev/hugepages:/dev/hugepages $TAG"
