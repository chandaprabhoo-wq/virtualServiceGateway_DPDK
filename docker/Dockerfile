FROM ubuntu:22.04

# Install dependencies
RUN apt-get update && apt-get install -y \
    build-essential \
    cmake \
    git \
    libelf-dev \
    libpcap-dev \
    pkg-config \
    python3 \
    python3-pip \
    && rm -rf /var/lib/apt/lists/*

# Build DPDK (simplified - in production, use pre-built wheels)
WORKDIR /tmp
RUN git clone --depth 1 --branch v21.11 https://github.com/DPDK/dpdk.git && \
    cd dpdk && \
    meson build && \
    ninja -C build install && \
    ldconfig && \
    cd / && rm -rf /tmp/dpdk

# Create app structure
WORKDIR /app
COPY . .

# Build vSG
RUN mkdir -p build && \
    cd build && \
    cmake .. && \
    make -j$(nproc)

# Configuration
COPY config/vsg.conf /etc/vsg/vsg.conf
COPY config/dpi_signatures.conf /etc/vsg/dpi_signatures.conf

# Runtime user
RUN useradd -m -s /bin/bash vsg

# Expose monitoring ports
EXPOSE 9090 8080

# Health check
HEALTHCHECK --interval=30s --timeout=10s --start-period=5s --retries=3 \
    CMD curl -f http://localhost:9090/health || exit 1

# Runtime configuration via environment
ENV VSG_CONFIG=/etc/vsg/vsg.conf
ENV VSG_WORKER_THREADS=4
ENV VSG_MAX_SUBSCRIBERS=1000000
ENV VSG_HUGEPAGES=/dev/hugepages

# Startup script
COPY docker/entrypoint.sh /entrypoint.sh
RUN chmod +x /entrypoint.sh

ENTRYPOINT ["/entrypoint.sh"]
CMD ["/app/build/vsg_gateway", "--config", "/etc/vsg/vsg.conf"]
