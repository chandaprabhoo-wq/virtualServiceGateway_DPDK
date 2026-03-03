# vSG Project - Workspace Instructions

## Project Setup Status

- [x] Workspace created and verified
- [x] Project structure scaffolded
- [x] Core headers and implementations created
- [x] Tests, configuration, and Docker setup complete
- [x] Comprehensive documentation generated

## Project Overview

**vSG (Virtual Service Gateway)**: High-performance packet processing data plane for telecom service providers.

### Key Features Implemented

1. **DPDK Packet Processing** (`src/dpdk/`)
   - PMD (Poll Mode Driver) abstraction with multi-queue RX/TX
   - Memory pool management with NUMA awareness
   - Lock-free ring queues for inter-core communication
   - Batch processor for cache-efficient processing
   - Multi-stage worker pipelines with backpressure

2. **UBB Metering** (`src/metering/`)
   - Per-subscriber, per-service byte accounting
   - Atomic lock-free counter updates
   - Configurable rollup periods and exports
   - Reconciliation engine for billing correctness

3. **QoE Analytics** (`src/analytics/`)
   - Flow-level KPI collection (loss, latency, jitter, throughput)
   - Service-level aggregation
   - Safe high-cardinality telemetry with sampling
   - Time-series DB integration

4. **DPI Classification** (`src/dpi/`)
   - Signature-based traffic classification
   - Timeout-protected classification (fast path safety)
   - Degraded mode for overload handling
   - Live signature updates without restarts

5. **Containerization** (`docker/`)
   - Dockerfile with DPDK and vSG build
   - docker-compose orchestration
   - Health checks and monitoring endpoints
   - Environment-based configuration injection

### Project Structure

```
vSG/
├── CMakeLists.txt              # Build configuration
├── include/                    # Public API headers
├── src/
│   ├── dpdk/                  # Packet processing core
│   ├── metering/              # UBB billing implementation
│   ├── analytics/             # QoE telemetry collection
│   ├── dpi/                   # Traffic classification
│   └── main.cpp               # Gateway entry point
├── tests/                     # Unit tests (metering, analytics, pipeline)
├── config/                    # Runtime configuration files
├── docker/                    # Docker build & orchestration
├── scripts/                   # Build and deployment automation
└── docs/                      # Comprehensive documentation

Documentation Files:
├── docs/architecture.md       # Overall design & component overview
├── docs/dpdk_design.md       # DPDK optimization & tuning
├── docs/scaling.md           # Multi-core scaling strategies
├── docs/metering.md          # UBB metering design & lifecycle
├── docs/analytics.md         # QoE metrics collection & aggregation
├── docs/dpi.md               # DPI classification & performance isolation
└── docs/containerization.md  # Docker & Kubernetes deployment
```

## Build Instructions

### Prerequisites

**Linux (Ubuntu 22.04+)**:
```bash
sudo apt-get install -y build-essential cmake libelf-dev libpcap-dev pkg-config
# Install DPDK: sudo apt-get install -y dpdk libdpdk-dev (or build from source)
```

**macOS** (Development only):
```bash
brew install cmake pkg-config
# DPDK build more complex; use Linux VM for real development
```

### Build Steps

```bash
# From project root
mkdir -p build
cd build
cmake -DCMAKE_BUILD_TYPE=Release ..
make -j$(nproc)

# Run tests
make test
# Or: cd .. && ./scripts/test.sh
```

### Build Output

```
build/
├── vsg_gateway           # Main executable
├── test_metering         # UBB metering tests
├── test_analytics        # QoE analytics tests
├── test_pipeline         # DPDK pipeline tests
└── CMakeFiles/           # Build artifacts
```

## Running the Gateway

### Development (Standalone)

```bash
# Without DPDK/hugepages (demo mode)
./build/vsg_gateway --config config/vsg.conf

# Expected output:
# vSG Gateway Starting
# ====================
# Initializing DPDK PMD...
# Initializing mempools...
# [Gateway running with test data...]
```

### Production (Containerized)

```bash
# Build Docker image
./scripts/docker_build.sh vsg:v1.0.0

# Run with docker-compose
cd docker && docker-compose up -d

# Check status
docker-compose ps
docker logs -f vsg-gateway

# Monitor metrics
curl http://localhost:8080/metrics | grep vsg_
```

### With Real NICs (Linux Only)

```bash
# Setup hugepages
sudo sysctl -w vm.nr_hugepages=1024

# Bind NICs to DPDK
sudo usertools/dpdk-devbind.py --bind vfio-pci 0000:01:00.0 0000:01:00.1

# Run with proper arguments
./build/vsg_gateway \
  --config config/vsg.conf \
  --worker-threads 8 \
  --max-subscribers 10000000
```

## Configuration

Edit `config/vsg.conf` for runtime settings:
- Worker thread count and NUMA placement
- Port RX/TX queue counts
- Mempool sizes and cache settings
- UBB metering parameters (rollup period, thresholds)
- QoE analytics sampling and windows
- DPI timeout and degradation settings
- Logging and monitoring configuration

## Testing

```bash
# Run all tests
./scripts/test.sh

# Or individual tests
./build/test_metering
./build/test_analytics
./build/test_pipeline

# Expected output: All tests passed!
```

## Performance Benchmarking

```bash
# Profile with perf (Linux only)
perf record -g ./build/vsg_gateway
perf report

# Target metrics:
# - Throughput: 25+ Gbps per core
# - Latency: p99 < 100 microseconds
# - Memory: < 1 GB per core with pooling
# - CPU: < 10% overhead for metering/analytics
```

## Documentation Structure

Each documentation file provides:
- **Architecture**: System design and component interactions
- **Design Decisions**: Trade-offs and justifications
- **Performance**: Optimization techniques and targets
- **Configuration**: Parameter tuning and examples
- **Troubleshooting**: Common issues and solutions
- **Testing**: Unit and integration test approaches

Start with `docs/architecture.md` for a high-level overview.

## Development Workflow

1. **Design Phase**: Understand requirements in `docs/architecture.md`
2. **Implement**: Modify headers in `include/`, implementations in `src/`
3. **Build**: `cd build && cmake .. && make -j$(nproc)`
4. **Test**: Run unit tests to verify functionality
5. **Profile**: Use `perf` to identify bottlenecks
6. **Optimize**: Apply techniques from relevant `docs/` files
7. **Container**: Package with `docker build` for deployment

## Key Design Principles

✓ **Lock-Free Design**: Atomic operations instead of mutexes
✓ **NUMA Aware**: Per-socket memory and core affinity
✓ **Batching**: Amortize overhead across 32-64 packets
✓ **Sampling**: Safe high-cardinality aggregation
✓ **Graceful Degradation**: Degrade QoS instead of crashing
✓ **Observable**: Comprehensive monitoring and metrics
✓ **Containerized**: Run anywhere with consistent config

## Next Steps

1. **Review Documentation**: Start with `docs/architecture.md`
2. **Study Components**: Understand each module's design
3. **Build & Test**: Verify all tests pass
4. **Profile**: Measure performance in your environment
5. **Customize**: Adapt signatures, thresholds, and scaling for your use case
6. **Deploy**: Use Docker/Kubernetes for production

## Important Notes

- **DPDK Dependency**: Most features require DPDK 21.11+
- **Platform**: Optimized for Linux; macOS development via VM
- **Hardware**: Best performance with modern CPUs (Intel Xeon, AMD EPYC) and NICs (e810, i40e)
- **Kernel**: Linux 5.8+ recommended for DPDK support
- **Performance**: Scaling targets assume proper hardware and tuning

## Support & Troubleshooting

See relevant `docs/` files for:
- Build issues: See `CMakeLists.txt` and compiler errors
- Runtime issues: Check `config/vsg.conf` and logs
- Performance: Review `docs/dpdk_design.md` and `docs/scaling.md`
- Billing accuracy: See `docs/metering.md` reconciliation section
- QoE issues: Review `docs/analytics.md` sampling strategies
- Container issues: Check `docs/containerization.md`
