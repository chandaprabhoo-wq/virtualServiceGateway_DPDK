# vSG: Getting Started Guide

Welcome to the vSG (Virtual Service Gateway) project! This guide will help you understand the architecture and get started with development.

## Quick Start (5 minutes)

### 1. Understand the Project

**vSG** is a high-performance packet processing gateway for telecom service providers with three core capabilities:

- **UBB Metering**: Accurate per-subscriber billing (1M+ concurrent)
- **QoE Analytics**: Quality of Experience metrics (loss, latency, throughput)
- **DPI Integration**: Traffic classification with performance isolation

Built on **DPDK** (Data Plane Development Kit) for 25+ Gbps/core throughput.

### 2. Review the Architecture

Read `docs/architecture.md` (10 min) to understand:
- Component stack (PMD → Metering → Analytics → DPI)
- Data flow (RX → Processing → TX)
- Performance targets and achievable scaling

### 3. Build the Project

```bash
mkdir -p build && cd build
cmake -DCMAKE_BUILD_TYPE=Release ..
make -j$(nproc)
```

### 4. Run Tests

```bash
make test
# or: ../scripts/test.sh
```

Expected output: **All tests passed!**

### 5. Run the Gateway

```bash
./vsg_gateway --config ../config/vsg.conf
```

You now have a working vSG! (In demo mode without real NICs)

## Deep Dive (30 minutes)

### Component Overview

Read these in order:

1. **`docs/dpdk_design.md`** (15 min)
   - DPDK packet processing fundamentals
   - Memory pooling and batching strategies
   - Performance optimizations

2. **`docs/scaling.md`** (10 min)
   - Multi-core architecture
   - Queue-to-core mapping and flow hashing
   - NUMA optimization

3. **`docs/metering.md`** (10 min)
   - UBB counter implementation
   - Rollup and reconciliation
   - Counter lifecycle

### Code Organization

```
Key Files to Understand:

1. Main entry point:
   src/main.cpp (100 lines)

2. DPDK core:
   include/pmd_driver.h        → Port management
   include/mempool_manager.h   → Buffer allocation
   include/ring_queue.h        → Inter-core comm
   include/worker_pipeline.h   → Processing threads

3. Business logic:
   include/ubb_meter.h         → Billing counters
   include/qoe_analytics.h     → Quality metrics
   include/dpi_classifier.h    → Traffic classification
```

## Understanding Data Flow

### Packet Path (What Happens to Each Packet)

```
1. RX from NIC
   └─ rte_eth_rx_burst() → 64 packets

2. Mempool allocation
   └─ Allocate descriptor for each

3. Metering stage
   └─ Update subscriber/service counters (atomic)

4. DPI classification
   └─ Classify traffic type (timeout-protected)

5. Analytics sampling
   └─ Record telemetry sample (1 in 10)

6. TX to NIC
   └─ rte_eth_tx_burst() → 64 packets

Latency: 20-75 microseconds per packet
Throughput: 25+ Gbps per core
```

### Code Mapping

```cpp
// src/dpdk/worker_pipeline.cpp:worker_loop()
while (running) {
    // Step 1: RX
    nb_rx = rx_burst(pkts);
    
    // Step 2-5: Process through pipeline
    for (auto& processor : processors_) {
        processor->process_burst(pkts, nb_rx);
    }
    
    // Step 6: TX
    tx_burst(port, queue, pkts, nb_rx);
}
```

## Hands-On: Modify Packet Processing

Let's add a simple feature: count packets by size.

### 1. Add Counter to Header

```cpp
// include/dpdk_types.h - add to PipelineStats:
volatile uint64_t small_pkts;   // < 256 bytes
volatile uint64_t medium_pkts;  // 256-1500 bytes
volatile uint64_t large_pkts;   // > 1500 bytes
```

### 2. Update Processing

```cpp
// src/dpdk/batch_processor.cpp - in process_single():
void process_single(struct rte_mbuf* pkt) {
    if (!pkt) return;
    
    uint16_t pkt_len = rte_pktmbuf_pkt_len(pkt);
    
    // Add to statistics
    if (pkt_len < 256) {
        stats.small_pkts++;
    } else if (pkt_len < 1500) {
        stats.medium_pkts++;
    } else {
        stats.large_pkts++;
    }
    
    // ... existing code ...
}
```

### 3. Rebuild

```bash
cd build
make -j$(nproc)
./vsg_gateway --config ../config/vsg.conf
```

Congratulations! You've modified the packet processing pipeline.

## Performance Tuning Guide

### Measure Performance

```bash
# Build release version
cd build
cmake -DCMAKE_BUILD_TYPE=Release ..
make

# Run with perf (Linux)
perf record -g ./vsg_gateway
perf report
```

### Key Metrics to Monitor

```
Throughput (Gbps):    = (RX bytes/sec) * 8 / 1e9
Packet rate (Mpps):   = (RX packets/sec) / 1e6
Latency (µs):         = Time from RX to TX per packet
CPU usage (%):        = CPU cycles per packet / (GHz * 1000)
Memory (MB):          = RSS size from 'top' or 'docker stats'
```

### Optimization Checklist

- [ ] Mempool size adequate (monitor avail_count)
- [ ] RX/TX queues = number of cores
- [ ] Workers pinned to cores (`taskset` or DPDK affinity)
- [ ] Batch size 32-64 packets
- [ ] Huge pages enabled (1024 × 2MB typical)
- [ ] Cache line alignment (64 bytes)
- [ ] No locks in fast path

## Deployment Workflow

### Development

```bash
# Edit code, test locally
./scripts/test.sh

# Rebuild
cd build && make

# Run standalone
./vsg_gateway --config ../config/vsg.conf
```

### Staging

```bash
# Build Docker image
./scripts/docker_build.sh vsg:staging

# Test with docker-compose
cd docker
docker-compose up -d

# Check logs
docker-compose logs -f

# Verify metrics
curl http://localhost:8080/metrics
```

### Production

```bash
# Build optimized image
./scripts/docker_build.sh vsg:v1.0.0

# Deploy with Kubernetes or orchestration
kubectl apply -f vsg-deployment.yaml

# Monitor
kubectl logs -f deployment/vsg-gateway
kubectl top nodes
```

## Common Questions

### Q: What's DPDK and why do we use it?

**A**: DPDK is a set of libraries/drivers for fast packet processing. Benefits:
- **PMD (Poll Mode Driver)**: No interrupts = lower latency
- **Hugepages**: Fewer TLB misses = faster access
- **Batching**: Process 64 packets with 1 function call
- **Result**: 100x+ faster than kernel stack

### Q: How does metering work?

**A**: Each (subscriber_id, service_id) pair has atomic byte counters:
```cpp
counter->uplink_bytes += pkt->len;    // Fast: 1-2 ns
counter->downlink_bytes += pkt->len;   // Lock-free!
```

Every 5 minutes, we "rollup" and export to billing system. No locks needed!

### Q: Can I change the number of workers?

**A**: Yes! In `config/vsg.conf`:
```ini
[dpdk]
worker_threads = 4  # Change to 2, 8, 16, etc.
```

Scales linearly up to ~16 cores (then diminishing returns).

### Q: How do I add new DPI signatures?

**A**: Edit `config/dpi_signatures.conf`:
```
# Add new line: id classification pattern offset depth
100 CUSTOM_PROTO MyPattern 0 512
```

Or programmatically:
```cpp
DPISignature sig;
sig.signature_id = 100;
sig.classification = ClassificationResult::CUSTOM_PROTO;
sig.pattern = "MyPattern";
classifier->update_signature(sig);
```

### Q: What if packets arrive too fast?

**A**: Backpressure handles it:
1. RX ring queue fills → Pause RX
2. Drop low-priority flows (DPI > Analytics > Metering)
3. When queue drains → Resume RX

See `docs/scaling.md` for details.

### Q: How do I monitor in production?

**A**: vSG exports Prometheus metrics:
```bash
curl http://localhost:8080/metrics | grep vsg_
# Output:
# vsg_rx_packets_total{port="0"} 1000000
# vsg_metering_updates_total 999500
# vsg_dpi_classifications_total{class="HTTP"} 500000
```

Create Grafana dashboards using these metrics.

## Troubleshooting

### Build Fails

**Check**:
1. DPDK installed: `pkg-config --cflags libdpdk`
2. CMake version: `cmake --version` (need 3.20+)
3. Compiler: `gcc --version` (need 9+)

**Solution**:
```bash
# Install dependencies
sudo apt-get install -y build-essential cmake libdpdk-dev
cd build && cmake .. && make clean && make
```

### Tests Fail

**Check logs**:
```bash
cd build && ctest --verbose
```

**Common issues**:
- Missing DPDK headers: Install `libdpdk-dev`
- Memory allocation failure: Increase hugepages or RAM

### Low Throughput

**Debug steps**:
1. Check mempool: `rte_mempool_avail_count()`
2. Monitor queues: `rte_ring_free_count()`
3. Profile: `perf record -g` then `perf report`

See `docs/dpdk_design.md` debugging section.

### Container Won't Start

```bash
# Check logs
docker logs vsg-gateway

# Common: Hugepages not available
# Fix: sudo sysctl -w vm.nr_hugepages=1024

# Common: NICs not found
# Check: docker run --privileged (must have this)
```

## Next Steps

1. **Read Documentation**: Dive into `docs/` files as needed
2. **Study Code**: Examine source implementations
3. **Experiment**: Modify and rebuild
4. **Benchmark**: Measure performance in your environment
5. **Customize**: Adapt for your use case (signatures, thresholds, etc.)
6. **Deploy**: Container workflow for production

## Additional Resources

### In This Project

- `README.md`: Project overview
- `CMakeLists.txt`: Build configuration
- `config/vsg.conf`: Runtime parameters
- `tests/`: Unit test examples

### External Documentation

- [DPDK Programmer's Guide](https://doc.dpdk.org/)
- [Intel PMD Documentation](https://doc.dpdk.org/guides/nics/)
- [Telecom Billing Standards (3GPP)](https://www.3gpp.org/)

## Getting Help

### Common Issues

1. **DPDK not found**: Install `libdpdk-dev` package
2. **Hugepages**: Set via `sudo sysctl -w vm.nr_hugepages=1024`
3. **NICs not detected**: Use `dpdk-devbind.py` to bind NICs
4. **Low performance**: Check `docs/dpdk_design.md` tuning section

### Debug Tips

```bash
# Check DPDK EAL status
./vsg_gateway --log-level=8  # DEBUG

# Monitor stats in real-time
watch 'curl http://localhost:8080/metrics | head -20'

# Profile with perf
perf record -F 99 ./vsg_gateway
perf report
```

## Summary

You now understand:
✓ What vSG does (packet processing + metering + analytics)
✓ How DPDK enables high performance (PMD, batching, hugepages)
✓ Architecture (components, data flow, scaling)
✓ How to build, test, and run
✓ How to monitor and optimize

**Happy coding!** 🚀
