# Containerization & Deployment Guide

## Docker Container Architecture

### Image Layers

```dockerfile
ubuntu:22.04 (base OS)
    ↓
build-essential, cmake, DPDK libs
    ↓
DPDK build & install
    ↓
vSG application build
    ↓
Configuration & entrypoint
    ↓
vsg:latest (final image)
```

**Image size**: ~800 MB (base + DPDK + app)

### Container Runtime

**Base OS**: Ubuntu 22.04 LTS (18-month support)
**libc**: glibc 2.35
**Kernel**: 5.13+ (for DPDK)

## Building the Container

### Prerequisites

```bash
# On host (Linux)
docker --version    # >= 20.10
docker-compose --version  # >= 1.29
```

### Build Process

```bash
# Full build from source
./scripts/docker_build.sh vsg:v1.0.0

# Or use docker-compose
cd docker
docker-compose build

# Check image
docker images | grep vsg
# vsg    latest    abc123def456    500MB    20 minutes ago
```

### Build Arguments (Optional)

```dockerfile
ARG DPDK_VERSION=21.11
ARG BUILD_WORKERS=4
ARG OPTIMIZATION_LEVEL=O3
```

## Running the Container

### Basic Run

```bash
docker run --rm -it \
  --privileged \
  --network host \
  -v /dev/hugepages:/dev/hugepages \
  -e VSG_WORKER_THREADS=4 \
  -e VSG_MAX_SUBSCRIBERS=1000000 \
  vsg:latest
```

### With docker-compose

```bash
cd docker
docker-compose up -d

# Check logs
docker-compose logs -f vsg-gateway

# Stats
docker stats vsg-gateway
```

## Configuration

### Environment Variables

| Variable | Default | Purpose |
|----------|---------|---------|
| VSG_CONFIG | /etc/vsg/vsg.conf | Config file path |
| VSG_WORKER_THREADS | 4 | Number of worker cores |
| VSG_MAX_SUBSCRIBERS | 1000000 | Max tracked subscribers |
| VSG_HUGEPAGES | /dev/hugepages | Hugepage mount point |
| VSG_LOG_LEVEL | INFO | Logging level |

### Config File Override

```bash
docker run \
  -e VSG_WORKER_THREADS=8 \
  -v ./config/vsg.conf:/etc/vsg/vsg.conf:ro \
  vsg:latest
```

### Volume Mounts

```bash
docker run \
  -v /dev/hugepages:/dev/hugepages \
  -v ./config:/etc/vsg:ro \
  -v ./logs:/var/log/vsg \
  -v ./data:/data/vsg \
  vsg:latest
```

## Deployment Modes

### Development (Localhost)

```bash
# Single container, limited resources
docker run --rm -it \
  -p 9090:9090 \
  -e VSG_WORKER_THREADS=2 \
  vsg:latest
```

**Config**: 2 workers, 100K subscribers, debug logging

### Staging (Testing)

```bash
# Container with resource limits
docker run -d \
  --name vsg-staging \
  --cpus 4 \
  --memory 4g \
  --memory-swap 4g \
  -e VSG_WORKER_THREADS=4 \
  -e VSG_LOG_LEVEL=INFO \
  vsg:latest
```

**Config**: 4 workers, 1M subscribers, info logging

### Production (Multiple Replicas)

```yaml
# docker-compose.yml
version: '3.9'
services:
  vsg-gw-1:
    image: vsg:v1.0.0
    hostname: vsg-gw-1
    environment:
      VSG_WORKER_THREADS: 8
      VSG_MAX_SUBSCRIBERS: 10000000
    ports:
      - "9090:9090"
    volumes:
      - /dev/hugepages:/dev/hugepages
      - vsg-logs-1:/var/log/vsg
    restart: always

  vsg-gw-2:
    image: vsg:v1.0.0
    hostname: vsg-gw-2
    environment:
      VSG_WORKER_THREADS: 8
      VSG_MAX_SUBSCRIBERS: 10000000
    ports:
      - "9091:9090"
    volumes:
      - /dev/hugepages:/dev/hugepages
      - vsg-logs-2:/var/log/vsg
    restart: always

volumes:
  vsg-logs-1:
  vsg-logs-2:
```

## Resource Requirements

### CPU

| Workload | Cores | Notes |
|----------|-------|-------|
| Dev/Test | 2-4 | Sufficient for testing |
| Small (1 Gbps) | 4 | ~250 Mbps per core |
| Medium (10 Gbps) | 8 | ~1.25 Gbps per core |
| Large (40 Gbps) | 16 | ~2.5 Gbps per core |

### Memory

| Component | Size | Notes |
|-----------|------|-------|
| OS & Libraries | 200 MB | Base system |
| Mempools | 512 MB | 64K per core × 8 cores |
| Counters | 256 MB | 2.5M subscriber/service pairs |
| RX/TX buffers | 64 MB | Per-core queues |
| **Total** | **~1 GB** | Per gateway instance |

### Network

```
Ingress NIC:  25 Gbps (Intel i40e or E810)
Egress NIC:   25 Gbps
OR
Single NIC with bidirectional (loopback testing)
```

### Storage

| Item | Size | Purpose |
|------|------|---------|
| Config | 10 KB | Runtime config |
| Logs | 1 GB/day | Rotation enabled |
| Rollups (optional) | 10 MB/day | JSON export |
| Temp data | 1 GB | Working directory |

## Health Checks

### Liveness Probe

```bash
curl -f http://localhost:9090/health || exit 1
```

**Response**:
```json
{
    "status": "alive",
    "uptime_seconds": 3600,
    "rx_pps": 1000000,
    "tx_pps": 995000
}
```

### Readiness Probe

```bash
curl -f http://localhost:9090/ready || exit 1
```

**Response**:
```json
{
    "ready": true,
    "workers": 4,
    "active_flows": 50000,
    "mempool_free": 95000
}
```

### Kubernetes Probes

```yaml
livenessProbe:
  httpGet:
    path: /health
    port: 9090
  initialDelaySeconds: 10
  periodSeconds: 30

readinessProbe:
  httpGet:
    path: /ready
    port: 9090
  initialDelaySeconds: 5
  periodSeconds: 10
```

## Logging

### Log Rotation

```ini
[logging]
file = /var/log/vsg/gateway.log
max_size_mb = 100
max_files = 10
```

**Behavior**: Rotate when file hits 100 MB, keep last 10 files = 1 GB max

### Docker Logging Driver

```json
{
    "log-driver": "json-file",
    "log-opts": {
        "max-size": "100m",
        "max-file": "10",
        "labels": "service=vsg-gateway"
    }
}
```

### View Logs

```bash
# Real-time
docker logs -f vsg-gateway

# Last 100 lines
docker logs --tail 100 vsg-gateway

# With timestamps
docker logs --timestamps vsg-gateway
```

## Monitoring & Metrics

### Prometheus Endpoint

```
GET http://localhost:8080/metrics
```

**Available metrics**:
```
vsg_rx_packets_total{port="0"}
vsg_rx_bytes_total{port="0"}
vsg_tx_packets_total{port="0"}
vsg_metering_updates_total{subscriber="*"}
vsg_dpi_classifications_total{class="HTTP"}
vsg_worker_cpu_percent{worker="0"}
vsg_mempool_utilization{socket="0"}
vsg_queue_depth{direction="rx"}
```

### Grafana Dashboard

```json
{
    "dashboard": {
        "title": "vSG Gateway",
        "panels": [
            {
                "title": "Throughput (Gbps)",
                "targets": [
                    "rate(vsg_rx_bytes_total[5m]) * 8 / 1e9"
                ]
            },
            {
                "title": "Packet Rate (Mpps)",
                "targets": [
                    "rate(vsg_rx_packets_total[5m]) / 1e6"
                ]
            },
            {
                "title": "CPU Utilization",
                "targets": [
                    "vsg_worker_cpu_percent"
                ]
            },
            {
                "title": "Memory Usage",
                "targets": [
                    "vsg_memory_usage_bytes"
                ]
            }
        ]
    }
}
```

## Troubleshooting

### Container Won't Start

```bash
# Check logs
docker logs vsg-gateway

# Common issues:
# 1. Hugepages not available
#    Fix: sudo sysctl -w vm.nr_hugepages=1024

# 2. NICs not found
#    Fix: Check --privileged flag and NIC driver

# 3. DPDK EAL init failure
#    Fix: Check kernel version (5.8+), CPU flags
```

### Low Throughput

```bash
# Check worker distribution
docker exec vsg-gateway cat /var/log/vsg/worker_stats.log

# Issues:
# 1. Uneven load (hash collision)
# 2. Slow mempool allocation
# 3. Insufficient hugepages

# Solutions:
# 1. Verify hash function quality
# 2. Increase mempool size
# 3. Allocate more hugepages
```

### Memory Leak

```bash
# Monitor memory growth
watch 'docker stats vsg-gateway | tail -1'

# Check for:
# 1. Uncleaned counters (idle timeout)
# 2. Ringbuffer growing
# 3. Log file growing

# Solutions:
# 1. Tune idle_timeout_sec in config
# 2. Check queue backpressure
# 3. Enable log rotation
```

## Deployment Checklist

- [ ] Docker image built and tested locally
- [ ] Configuration files prepared (vsg.conf, signatures.conf)
- [ ] Hugepages configured on target hosts (1024 pages × 2MB)
- [ ] NICs installed and driver installed
- [ ] Network configuration (VLANs, IP addresses)
- [ ] Monitoring setup (Prometheus, Grafana)
- [ ] Logging setup (centralized logging)
- [ ] Health checks configured
- [ ] Rollback procedure documented
- [ ] Performance baseline established

## Scaling & Load Balancing

### Multiple Gateway Instances

```
Packets
  ↓
Load Balancer (ECMP, RSS on ingress switch)
  ├→ vsg-gw-1 (Core 0-3)
  ├→ vsg-gw-2 (Core 4-7)
  └→ vsg-gw-3 (Core 8-11)
  ↓
Egress switch
  ↓
Destination
```

**Steering**: By 5-tuple hash to ensure flow affinity

### Kubernetes Deployment

```yaml
apiVersion: apps/v1
kind: StatefulSet
metadata:
  name: vsg-gateway
spec:
  serviceName: vsg-gateway
  replicas: 3
  selector:
    matchLabels:
      app: vsg-gateway
  template:
    metadata:
      labels:
        app: vsg-gateway
    spec:
      affinity:
        nodeAffinity:
          requiredDuringSchedulingIgnoredDuringExecution:
            nodeSelectorTerms:
            - matchExpressions:
              - key: dedicated
                operator: In
                values:
                - vsg-gateway
      containers:
      - name: vsg-gateway
        image: vsg:v1.0.0
        securityContext:
          privileged: true
        resources:
          requests:
            cpu: 8
            memory: 4Gi
          limits:
            cpu: 8
            memory: 4Gi
        volumeMounts:
        - name: hugepages
          mountPath: /dev/hugepages
        - name: config
          mountPath: /etc/vsg
        livenessProbe:
          httpGet:
            path: /health
            port: 9090
          initialDelaySeconds: 10
      volumes:
      - name: hugepages
        emptyDir:
          medium: HugePages
      - name: config
        configMap:
          name: vsg-config
```

## References

- Docker Best Practices: https://docs.docker.com/develop/dev-best-practices/
- Kubernetes Networking: https://kubernetes.io/docs/concepts/services-networking/
- DPDK in Containers: DPDK official guides
