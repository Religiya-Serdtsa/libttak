#!/usr/bin/env bash

# LibTTAK Multi-platform Cross-Build Script
# Supported OS: Linux, Windows, FreeBSD, OpenBSD, NetBSD, Darwin
# Supported Arch: x86_64, aarch64, riscv64, ppc64le, mips64el

set -e

# Configuration
PROJECT_NAME="libttak"
DIST_DIR="dist"
LOG_DIR="logs"

# Output colors
BLUE='\033[0;34m'
GREEN='\033[0;32m'
RED='\033[0;31m'
NC='\033[0m'

mkdir -p "$DIST_DIR" "$LOG_DIR"

# Common optimization flags overriding Makefile defaults for cross-compilation
BASE_PERF_FLAGS="-O3 -pipe -flto -ffat-lto-objects -fomit-frame-pointer -funroll-loops \
                -fstrict-aliasing -ffunction-sections -fdata-sections -fvisibility=hidden -DNDEBUG"

# Target Matrix: OS:ARCH:CC:AR:ARCH_FLAGS
TARGETS=(
    # Linux
    "linux:amd64:x86_64-linux-gnu-gcc:x86_64-linux-gnu-ar:-march=x86-64-v3"
    "linux:arm64:aarch64-linux-gnu-gcc:aarch64-linux-gnu-ar:-march=armv8-a"
    "linux:riscv64:riscv64-linux-gnu-gcc:riscv64-linux-gnu-ar:-march=rv64gc"
    "linux:ppc64le:powerpc64le-linux-gnu-gcc:powerpc64le-linux-gnu-ar:-mcpu=power8"
    "linux:mips64el:mips64el-linux-gnuabi64-gcc:mips64el-linux-gnuabi64-ar:-march=mips64r2"

    # Windows
    "windows:amd64:x86_64-w64-mingw32-gcc:x86_64-w64-mingw32-ar:-march=x86-64"
    "windows:arm64:aarch64-w64-mingw32-gcc:aarch64-w64-mingw32-ar:-march=armv8-a"

    # FreeBSD
    "freebsd:amd64:x86_64-pc-freebsd14-gcc:x86_64-pc-freebsd14-ar:-march=x86-64"
    "freebsd:arm64:aarch64-unknown-freebsd14-gcc:aarch64-unknown-freebsd14-ar:-march=armv8-a"

    # OpenBSD
    "openbsd:amd64:x86_64-unknown-openbsd7.4-gcc:x86_64-unknown-openbsd7.4-ar:-march=x86-64"

    # NetBSD
    "netbsd:amd64:x86_64-unknown-netbsd10.0-gcc:x86_64-unknown-netbsd10.0-ar:-march=x86-64"

    # Darwin
    "darwin:amd64:x86_64-apple-darwin21.1-clang:x86_64-apple-darwin21.1-ar:-march=x86-64"
    "darwin:arm64:aarch64-apple-darwin21.1-clang:aarch64-apple-darwin21.1-ar:-march=armv8-a"
)

build_target() {
    local os=$1
    local arch=$2
    local cc=$3
    local ar=$4
    local arch_flags=$5
    local tag="${PROJECT_NAME}-${os}-${arch}"
    local out="${DIST_DIR}/${tag}"

    echo -e "${BLUE}[BUILDING]${NC} $os/$arch with $cc"

    # Check for compiler availability
    if ! command -v "$cc" &> /dev/null; then
        echo -e "--> Skipping $tag: $cc not found."
        return
    fi

    make clean > /dev/null

    # Override Makefile flags
    local final_stack_flags="$BASE_PERF_FLAGS $arch_flags"
    local extra_cflags="-fPIC"

    # Set platform-specific preprocessor macros
    case $os in
        windows)
            extra_cflags+=" -D_WIN32_WINNT=0x0600 -D_GNU_SOURCE"
            ;;
        freebsd|openbsd|netbsd)
            extra_cflags+=" -D__BSD_VISIBLE=1"
            ;;
        darwin)
            extra_cflags+=" -D_DARWIN_C_SOURCE"
            ;;
    esac

    # Execute make with toolchain and flag overrides
    if CC="$cc" AR="$ar" PERF_STACK_FLAGS="$final_stack_flags" EXTRA_CFLAGS="$extra_cflags" make -j$(nproc) > "${LOG_DIR}/${tag}.log" 2>&1; then
        mkdir -p "${out}/include" "${out}/lib"
        cp -r include/* "${out}/include/"
        cp lib/libttak.a "${out}/lib/"

        # Package artifacts
        tar -czf "${DIST_DIR}/${tag}.tar.gz" -C "$DIST_DIR" "$tag"
        rm -rf "$out"
        echo -e "${GREEN}[DONE]${NC} Artifact: ${tag}.tar.gz"
    else
        echo -e "${RED}[ERROR]${NC} Build failed. Check ${LOG_DIR}/${tag}.log"
    fi
}

echo "Starting Multi-platform Cross-Build Process..."

for target in "${TARGETS[@]}"; do
    IFS=':' read -r os arch cc ar a_flags <<< "$target"
    build_target "$os" "$arch" "$cc" "$ar" "$a_flags"
done

echo "Build process finished. Artifacts are in '$DIST_DIR'."
