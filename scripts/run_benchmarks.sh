#!/usr/bin/env bash

set -euo pipefail

if [[ $# -gt 1 ]]; then
    echo "usage: $0 [experiment-id]" >&2
    exit 2
fi

source_dir=$(realpath "$(dirname "$0")/..")
experiment_id=${1:-"m3.1-baseline-$(date -u +%Y%m%dT%H%M%SZ)"}
result_dir="$source_dir/benchmarks/results/$experiment_id"
raw_file="$result_dir/raw.jsonl"
seed=20260912
repetitions=${HPLF_BENCHMARK_REPETITIONS:-5}
warmup_seconds=${HPLF_BENCHMARK_WARMUP_SECONDS:-1}
measured_seconds=${HPLF_BENCHMARK_SECONDS:-3}
live_slots=${HPLF_BENCHMARK_LIVE_SLOTS:-4096}

if [[ ! "$experiment_id" =~ ^[A-Za-z0-9._-]+$ ]]; then
    echo "experiment id may contain only letters, digits, dot, underscore and dash" >&2
    exit 2
fi
if [[ ! "$repetitions" =~ ^[1-9][0-9]*$ ]]; then
    echo "HPLF_BENCHMARK_REPETITIONS must be a positive integer" >&2
    exit 2
fi
if [[ -e "$result_dir" ]]; then
    echo "result directory already exists: $result_dir" >&2
    exit 1
fi

cmake --preset release -S "$source_dir"
cmake --build --preset release -j 2

commit=$(git -C "$source_dir" rev-parse HEAD)
if [[ -z "$(git -C "$source_dir" status --porcelain=v1)" ]]; then
    dirty=false
else
    dirty=true
fi
mkdir -p "$result_dir"
kernel=$(uname -r)
architecture=$(uname -m)
logical_cpus=$(getconf _NPROCESSORS_ONLN)
page_size=$(getconf PAGESIZE)
memory_bytes=$(awk '/^MemTotal:/ {print $2 * 1024}' /proc/meminfo)
cpu_model=$(lscpu | awk -F: '/^Model name:/ && !found {value=$2; sub(/^[[:space:]]+/, "", value); print value; found=1}')
if [[ "$kernel" == *microsoft* || "$kernel" == *Microsoft* ]]; then
    virtualized=true
    environment_name=WSL2
else
    virtualized=false
    environment_name=Linux
fi

printf '%s\n' \
    "{\"schema\":\"hplf-benchmark-environment-v1\",\"commit\":\"$commit\",\"dirty\":$dirty,\"environment\":\"$environment_name\",\"kernel\":\"$kernel\",\"architecture\":\"$architecture\",\"logical_cpus\":$logical_cpus,\"page_size\":$page_size,\"memory_bytes\":$memory_bytes,\"cpu_model\":\"$cpu_model\",\"compiler\":\"gcc-$(gcc -dumpfullversion)\",\"build_type\":\"Release\"}" \
    > "$result_dir/environment.json"

printf '%s\n' \
    "Command: scripts/run_benchmarks.sh $experiment_id" \
    "Matrix: fixed workload, 1 thread, seed $seed, $repetitions repetitions per variant" \
    "Durations: ${warmup_seconds}s warmup, ${measured_seconds}s measured" \
    "Order: libc/hplf alternates by repetition" \
    "Interpretation: foundational baseline rows only; no performance conclusion is made here." \
    > "$result_dir/command.txt"

for ((repetition = 1; repetition <= repetitions; ++repetition)); do
    if ((repetition % 2 == 1)); then
        variants=(libc locked)
    else
        variants=(locked libc)
    fi
    for variant in "${variants[@]}"; do
        executable="$source_dir/build/release/benchmarks/hplf_bench_$variant"
        HPLF_BENCHMARK_COMMIT="$commit" \
        HPLF_BENCHMARK_DIRTY="$dirty" \
            "$executable" \
                --workload fixed \
                --threads 1 \
                --seed "$seed" \
                --warmup-seconds "$warmup_seconds" \
                --seconds "$measured_seconds" \
                --live-slots "$live_slots" \
                --repetition "$repetition" \
                --output "$raw_file"
    done
done

echo "wrote $raw_file"
