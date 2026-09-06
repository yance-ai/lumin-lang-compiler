#!/usr/bin/env bash
set -e

SCRIPT_DIR=$(cd "$(dirname "${BASH_SOURCE[0]}")" &>/dev/null && pwd)
cd "${SCRIPT_DIR}" || exit 1

BIN_BIN="./bin/lumin"
SDK_SYSROOT=$(xcrun --sdk macosx --show-sdk-path)

usage() {
cat <<EOF
Usage: ./build.sh [command]

Commands:
    check        Check toolchain env (bison>=3.x, flex)
    build        Build compiler binary (default, native host → bin/lumin)
    universal    Build macOS universal fat‑binary (arm64 + x86_64)
    cross        Build all multi‑arch binaries (darwin‑arm64/x86_64/universal)
    compiler     Run compiled binary: compile tests/sample.lm
    rebuild      Distclean + full rebuild native host binary
    clean        Clean build artifacts, keep generated parser files
    distclean    Full clean, remove flex/bison generated sources
    run          Build and run compiler REPL
    help         Show this help

Examples:
    ./build.sh check
    ./build.sh build
    ./build.sh universal
    ./build.sh cross
    ./build.sh compiler
    ./build.sh rebuild
    ./build.sh run
EOF
}

build_arch() {
    local arch_target="$1"
    local out_bin="$2"
    echo "==> building arch: ${arch_target}"
    # 递归删除 src 下全部 .o，修复 glob 不能进入子目录的问题
    find src -name "*.o" -delete
    make all CFLAGS="-Wall -Wextra -g -I./src -I./generated -target ${arch_target} --sysroot ${SDK_SYSROOT}"
    cp "${BIN_BIN}" "${out_bin}"
}

case "${1:-build}" in
    check)
        echo "==> Check toolchain"
        make check-env
        ;;
    build|"")
        echo "==> Build native host: ${BIN_BIN}"
        make clean
        make distclean
        make all
        ;;
    universal)
        echo "==> Build macOS universal fat‑binary"
        make clean
        make distclean
        rm -rf bin
        build_arch arm64-apple-darwin ./bin/lumin-darwin-arm64
        build_arch x86_64-apple-darwin ./bin/lumin-darwin-x86_64
        lipo -create ./bin/lumin-darwin-arm64 ./bin/lumin-darwin-x86_64 -output ./bin/lumin-darwin-universal
        echo "==> universal binary: ./bin/lumin-darwin-universal"
        ;;
    cross)
        echo "==> Build multi‑arch binaries under bin/"
        make clean
        make distclean
        rm -rf bin
        build_arch arm64-apple-darwin ./bin/lumin-darwin-arm64
        build_arch x86_64-apple-darwin ./bin/lumin-darwin-x86_64
        lipo -create ./bin/lumin-darwin-arm64 ./bin/lumin-darwin-x86_64 -output ./bin/lumin-darwin-universal
        echo "note: linux‑amd64 requires zig‑cc, skip on native clang"
        ls -lh bin/
        ;;
    compiler)
        echo "==> Using ${BIN_BIN} compile tests/sample.lm"
        if [ ! -x "${BIN_BIN}" ]; then
            echo "ERROR: ${BIN_BIN} missing, run ./build.sh build first"
            exit 1
        fi
        ${BIN_BIN} -c tests/sample.lm
        ;;
    rebuild)
        echo "==> Rebuild native host binary"
        make clean
        make distclean
        make all
        ;;
    clean)
        echo "==> Clean artifacts, keep parser generated files"
        make clean
        ;;
    distclean)
        echo "==> Distclean, remove parser generated files"
        make distclean
        ;;
    run)
        echo "==> Build & run REPL"
        make all
        exec ${BIN_BIN}
        ;;
    help|--help|-h)
        usage
        ;;
    *)
        echo "Error: unknown command '$1'"
        usage
        exit 1
        ;;
esac
