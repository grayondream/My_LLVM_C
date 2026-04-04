#!/bin/bash

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(dirname "$SCRIPT_DIR")"
BUILD_DIR="${PROJECT_ROOT}/build"
BUILD_TYPE="${BUILD_TYPE:-Release}"

set -e

echo "=== Build Configuration ==="
echo "Project Root: ${PROJECT_ROOT}"
echo "Build Directory: ${BUILD_DIR}"
echo "Build Type: ${BUILD_TYPE}"
echo "VCPKG_ROOT: ${VCPKG_ROOT}"
echo "==========================="

if [ ! -d "${BUILD_DIR}" ]; then
    echo "Creating build directory..."
    mkdir -p "${BUILD_DIR}"
fi

cd "${BUILD_DIR}"

echo "Running CMake configuration..."
cmake .. \
    -DCMAKE_BUILD_TYPE="${BUILD_TYPE}" \
    -DCMAKE_TOOLCHAIN_FILE="${VCPKG_ROOT}/scripts/buildsystems/vcpkg.cmake"

echo "Building project..."
cmake --build . --config "${BUILD_TYPE}" -j$(nproc)

echo "Build completed successfully!"
echo "Executable: ${BUILD_DIR}/bin/my_llvm_c"
