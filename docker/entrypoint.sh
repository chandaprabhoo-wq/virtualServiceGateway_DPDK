#!/bin/bash
# vSG Gateway Entrypoint Script

set -e

echo "Starting vSG Gateway..."

# Setup hugepages if running in container
if [[ ! -z "${VSG_HUGEPAGES}" ]]; then
    echo "Setting up hugepages..."
    echo 1024 > /proc/sys/vm/nr_hugepages
    mkdir -p ${VSG_HUGEPAGES}
fi

# Export runtime configuration to config file
if [[ ! -z "${VSG_WORKER_THREADS}" ]]; then
    sed -i "s/worker_threads = .*/worker_threads = ${VSG_WORKER_THREADS}/" "${VSG_CONFIG}"
fi

if [[ ! -z "${VSG_MAX_SUBSCRIBERS}" ]]; then
    sed -i "s/max_subscribers = .*/max_subscribers = ${VSG_MAX_SUBSCRIBERS}/" "${VSG_CONFIG}"
fi

# Verify configuration
echo "Configuration:"
cat "${VSG_CONFIG}"

# Run the gateway
echo ""
echo "vSG Gateway starting with configuration: ${VSG_CONFIG}"
exec "$@"
