# vSG: Virtual Service Gateway Data Plane

High-performance packet processing gateway with UBB metering, QoE analytics, and DPI integration.

## Features

- **DPDK-based packet processing**: PMD driver with multi-queue RX/TX, mempools, and rings
- **Multi-core scaling**: Queue-to-core mapping, flow hashing, worker pipelines with backpressure
- **Performance optimization**: Cache locality, lock avoidance, NUMA correctness
- **UBB metering**: Per-subscriber/service byte accounting with reconciliation support
- **QoE analytics**: Flow/service KPIs, telemetry aggregation, high-cardinality handling
- **DPI integration**: Classification pipeline with signature updates and performance isolation
- **Containerized**: Docker packaging with clean runtime configuration

## Project Structure

```
vSG/
├── include/              # Public headers
├── src/
│   ├── dpdk/            # DPDK packet processing core
│   ├── metering/        # UBB metering implementation
│   ├── analytics/       # QoE analytics
│   ├── dpi/             # DPI classifier
│   └── main.cpp         # Gateway entry point
├── tests/               # Unit/integration tests
├── config/              # Configuration files
├── docker/              # Dockerfile and scripts
├── scripts/             # Build and deployment scripts
└── CMakeLists.txt       # Build configuration
```

## Requirements

- C++17 compiler (GCC 9+ or Clang 10+)
- DPDK 21.11 or later
- CMake 3.20+
- Linux kernel 5.8+

## Building

```bash
mkdir build && cd build
cmake ..
make -j$(nproc)
make test
```

## Configuration

Edit `config/vsg.conf` with your deployment parameters:
- Port mappings
- Worker thread count
- Memory pool sizes
- Metering and analytics options

## Running

```bash
# Set hugepages
sudo sysctl -w vm.nr_hugepages=1024

# Run gateway
./build/vsg_gateway --config config/vsg.conf
```

## Docker Deployment

```bash
docker build -f docker/Dockerfile -t vsg:latest .
docker run --rm -it \
  --privileged \
  --network host \
  -v /dev/hugepages:/dev/hugepages \
  vsg:latest
```

## Performance Targets

- **Throughput**: 25 Gbps+ per core
- **Packet Rate**: 100+ Mpps (million packets/sec)
- **Tail Latency**: p99 < 100 microseconds
- **Memory efficiency**: <1 GB per core with pooling

## Documentation

- [DPDK Core Design](docs/dpdk_design.md)
- [Multi-core Scaling](docs/scaling.md)
- [UBB Metering Architecture](docs/metering.md)
- [QoE Analytics](docs/analytics.md)
- [DPI Integration](docs/dpi.md)
- [Container Operations](docs/containerization.md)

## License

Proprietary - Telecom Service Provider
