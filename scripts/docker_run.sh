#!/bin/bash
# Docker run script for vSG gateway

set -e

WORKERS="${1:-4}"
SUBSCRIBERS="${2:-1000000}"
TAG="${3:-vsg:latest}"

echo "Starting vSG Gateway Container"
echo "Workers: $WORKERS"
echo "Max Subscribers: $SUBSCRIBERS"
echo ""

# Setup hugepages (if on Linux host)
if command -v sysctl &> /dev/null; then
    echo "Setting up hugepages..."
    sudo sysctl -w vm.nr_hugepages=1024
fi

docker run \
    --rm \
    -it \
    --privileged \
    --network host \
    -v /dev/hugepages:/dev/hugepages \
    -e VSG_WORKER_THREADS="$WORKERS" \
    -e VSG_MAX_SUBSCRIBERS="$SUBSCRIBERS" \
    "$TAG"
