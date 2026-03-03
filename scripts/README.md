# Build and Deployment Scripts

This directory contains automation scripts for building and deploying the vSG gateway.

## Available Scripts

### build.sh
Builds the vSG project with DPDK support.

```bash
./scripts/build.sh [BUILD_TYPE]
```

Build types:
- `release` (default): Optimized build
- `debug`: Build with debug symbols

### test.sh
Runs all unit tests.

```bash
./scripts/test.sh
```

### docker_build.sh
Builds the Docker container image.

```bash
./scripts/docker_build.sh [TAG]
```

Default tag: `vsg:latest`

### docker_run.sh
Runs the vSG container with proper resource configuration.

```bash
./scripts/docker_run.sh [WORKERS] [SUBSCRIBERS]
```

Default: 4 workers, 1M subscribers

### deploy.sh
Full deployment pipeline (build + test + containerize + run).

```bash
./scripts/deploy.sh [ENVIRONMENT]
```

Environments:
- `dev`: Development mode (localhost)
- `staging`: Staging deployment
- `prod`: Production deployment

## Performance Profiling

### profile.sh
Collect performance metrics using perf.

```bash
./scripts/profile.sh [DURATION_SEC]
```

Generates flame graphs and call graphs.

## Monitoring

### monitor.sh
Real-time monitoring dashboard.

```bash
./scripts/monitor.sh
```

Monitors:
- Packet throughput (Mpps)
- Bandwidth (Gbps)
- CPU utilization per core
- Memory usage
- Queue depths
- Metering and analytics metrics
