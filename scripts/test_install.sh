#!/usr/bin/env bash

set -euo pipefail

if [[ $# -ne 4 ]]; then
    echo "usage: $0 source-dir binary-dir sanitizer compiler" >&2
    exit 2
fi

source_dir=$(realpath "$1")
binary_dir=$(realpath "$2")
sanitizer=$3
compiler=$4
smoke_root="$binary_dir/install-smoke"
prefix="$smoke_root/prefix"

case "$smoke_root" in
    "$binary_dir"/install-smoke) ;;
    *) echo "refusing unsafe smoke directory: $smoke_root" >&2; exit 2 ;;
esac

cmake -E rm -rf "$smoke_root"
cmake --install "$binary_dir" --prefix "$prefix"

for mode in STATIC SHARED; do
    client_build="$smoke_root/client-${mode,,}"
    cmake -S "$source_dir/tests/installed_client" -B "$client_build" -G Ninja \
        -DCMAKE_BUILD_TYPE=Release \
        -DCMAKE_C_COMPILER="$compiler" \
        -DCMAKE_EXPORT_COMPILE_COMMANDS=ON \
        -DHPLF_ROOT="$prefix" \
        -DHPLF_LINK_MODE="$mode" \
        -DHPLF_SANITIZER="$sanitizer"
    cmake --build "$client_build"

    if grep -Fq "$source_dir/src" "$client_build/compile_commands.json"; then
        echo "installed client leaked a source-internal include path" >&2
        exit 1
    fi
done

static_client="$smoke_root/client-static/hplf_installed_client"
shared_client="$smoke_root/client-shared/hplf_installed_client"
shared_library="$prefix/lib/libhplf.so.0"
exported_symbols="$smoke_root/exported-symbols.txt"

if readelf -d "$static_client" | grep 'libhplf' >/dev/null; then
    echo "static client unexpectedly depends on a shared hplf library" >&2
    exit 1
fi
readelf -d "$shared_client" |
    grep 'Shared library: \[libhplf.so.0\]' >/dev/null

nm -D --defined-only "$shared_library" | awk '{print $3}' > "$exported_symbols"
for symbol in hplf_malloc hplf_free hplf_calloc hplf_realloc \
              hplf_thread_flush hplf_trim_quiescent; do
    grep -qx "$symbol" "$exported_symbols"
done
if [[ $(awk '/^hplf_/ {count++} END {print count+0}' "$exported_symbols") -ne 6 ]]; then
    echo "shared library exports an unexpected hplf symbol" >&2
    exit 1
fi
if readelf -d "$shared_library" "$static_client" "$shared_client" |
   grep 'libstdc++' >/dev/null; then
    echo "C clients unexpectedly depend on the C++ runtime" >&2
    exit 1
fi

"$static_client"
LD_LIBRARY_PATH="$prefix/lib" "$shared_client"
