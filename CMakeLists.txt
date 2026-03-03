cmake_minimum_required(VERSION 3.20)
project(vSG VERSION 1.0.0 LANGUAGES CXX C)

# Set C++ standard
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -march=native -O3 -g")

# Find DPDK
find_package(PkgConfig REQUIRED)
pkg_check_modules(DPDK REQUIRED libdpdk)

# Include directories
include_directories(${CMAKE_SOURCE_DIR}/include)
include_directories(${DPDK_INCLUDE_DIRS})

# Add libraries
add_library(vsg_dpdk_core
    src/dpdk/pmd_driver.cpp
    src/dpdk/mempool_manager.cpp
    src/dpdk/ring_queue.cpp
    src/dpdk/worker_pipeline.cpp
    src/dpdk/batch_processor.cpp
)

add_library(vsg_metering
    src/metering/ubb_meter.cpp
    src/metering/counter_manager.cpp
    src/metering/reconciliation.cpp
)

add_library(vsg_analytics
    src/analytics/qoe_collector.cpp
    src/analytics/flow_kpi.cpp
    src/analytics/telemetry_aggregator.cpp
)

add_library(vsg_dpi
    src/dpi/classifier_pipeline.cpp
    src/dpi/signature_manager.cpp
    src/dpi/dpi_worker.cpp
)

# Main executable
add_executable(vsg_gateway
    src/main.cpp
)

target_link_libraries(vsg_gateway
    vsg_dpdk_core
    vsg_metering
    vsg_analytics
    vsg_dpi
    ${DPDK_LIBRARIES}
)

# Example: Minimal DPDK Application
add_executable(minimal_dpdk
    examples/minimal_dpdk.cpp
)

target_link_libraries(minimal_dpdk
    ${DPDK_LIBRARIES}
)

# Tests
enable_testing()
add_executable(test_pipeline tests/test_pipeline.cpp)
target_link_libraries(test_pipeline vsg_dpdk_core ${DPDK_LIBRARIES})
add_test(NAME PipelineTest COMMAND test_pipeline)

add_executable(test_metering tests/test_metering.cpp)
target_link_libraries(test_metering vsg_metering)
add_test(NAME MeteringTest COMMAND test_metering)

add_executable(test_analytics tests/test_analytics.cpp)
target_link_libraries(test_analytics vsg_analytics)
add_test(NAME AnalyticsTest COMMAND test_analytics)
