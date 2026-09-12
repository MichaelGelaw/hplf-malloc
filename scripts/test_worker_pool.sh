#!/usr/bin/env bash

set -euo pipefail

if [[ $# -ne 3 ]]; then
    echo "usage: $0 libc-binary hplf-binary mode" >&2
    exit 2
fi

libc_binary=$1
hplf_binary=$2
mode=$3

string_field() {
    local row=$1
    local name=$2

    sed -n "s/.*\"$name\":\"\([^\"]*\)\".*/\1/p" <<<"$row"
}

number_field() {
    local row=$1
    local name=$2

    sed -n "s/.*\"$name\":\([0-9][0-9]*\).*/\1/p" <<<"$row"
}

require_equal() {
    local actual=$1
    local expected=$2
    local description=$3

    if [[ "$actual" != "$expected" ]]; then
        echo "$description: expected $expected, got $actual" >&2
        exit 1
    fi
}

validate_success() {
    local row=$1
    local allocator=$2
    local jobs=$3
    local expected
    local observed

    require_equal "$(string_field "$row" schema)" hplf-worker-pool-v1 schema
    require_equal "$(string_field "$row" status)" ok status
    require_equal "$(string_field "$row" allocator)" "$allocator" allocator
    require_equal "$(number_field "$row" jobs_requested)" "$jobs" jobs_requested
    require_equal "$(number_field "$row" jobs_produced)" "$jobs" jobs_produced
    require_equal "$(number_field "$row" jobs_consumed)" "$jobs" jobs_consumed
    require_equal "$(number_field "$row" duplicate_jobs)" 0 duplicate_jobs
    require_equal "$(number_field "$row" invalid_jobs)" 0 invalid_jobs
    require_equal "$(number_field "$row" missing_jobs)" 0 missing_jobs
    require_equal "$(number_field "$row" abandoned_jobs)" 0 abandoned_jobs
    require_equal "$(number_field "$row" exit_status)" 0 exit_status
    if [[ "$allocator" == hplf && "$jobs" -ne 0 &&
          "$(number_field "$row" trimmed_bytes)" -eq 0 ]]; then
        echo "hplf completed jobs without releasing any quiescent slab" >&2
        exit 1
    fi
    expected=$(string_field "$row" expected_checksum)
    observed=$(string_field "$row" checksum)
    require_equal "$observed" "$expected" checksum
}

run_pair() {
    local workers=$1
    local jobs=$2
    local seed=$3
    local capacity=$4
    local libc_row
    local hplf_row
    local libc_checksum
    local hplf_checksum

    libc_row=$("$libc_binary" --workers "$workers" --jobs "$jobs" \
        --seed "$seed" --queue-capacity "$capacity")
    hplf_row=$("$hplf_binary" --workers "$workers" --jobs "$jobs" \
        --seed "$seed" --queue-capacity "$capacity")
    validate_success "$libc_row" libc "$jobs"
    validate_success "$hplf_row" hplf "$jobs"
    libc_checksum=$(string_field "$libc_row" checksum)
    hplf_checksum=$(string_field "$hplf_row" checksum)
    require_equal "$hplf_checksum" "$libc_checksum" adapter_checksum
    printf '%s\n' "$hplf_checksum"
}

run_expected_failure() {
    local expected_code=$1
    local expected_status=$2
    shift 2
    local row
    local code

    set +e
    row=$("$hplf_binary" "$@")
    code=$?
    set -e
    require_equal "$code" "$expected_code" failure_exit_status
    require_equal "$(string_field "$row" status)" "$expected_status" failure_status
    printf '%s\n' "$row"
}

run_pair 1 0 17 1 >/dev/null
run_pair 1 1 17 1 >/dev/null
single_checksum=$(run_pair 1 257 19 1)
multi_checksum=$(run_pair 4 257 19 1)
require_equal "$multi_checksum" "$single_checksum" worker_count_checksum
run_pair 4 1000 23 17 >/dev/null

allocation_row=$(run_expected_failure 3 allocation_failure \
    --workers 4 --jobs 100 --seed 29 --queue-capacity 3 \
    --fail-allocation-at 37)
require_equal "$(number_field "$allocation_row" allocation_attempts)" 37 \
    allocation_attempts
require_equal "$(number_field "$allocation_row" jobs_produced)" 36 \
    allocation_jobs_produced
require_equal "$(number_field "$allocation_row" jobs_consumed)" 36 \
    allocation_jobs_consumed
require_equal "$(number_field "$allocation_row" abandoned_jobs)" 0 \
    allocation_abandoned_jobs
if [[ "$(number_field "$allocation_row" trimmed_bytes)" -eq 0 ]]; then
    echo "allocation-failure cleanup did not release a quiescent slab" >&2
    exit 1
fi

thread_row=$(run_expected_failure 4 thread_failure \
    --workers 4 --jobs 100 --seed 31 --queue-capacity 3 \
    --fail-thread-at 3)
require_equal "$(number_field "$thread_row" workers_started)" 2 workers_started
require_equal "$(number_field "$thread_row" jobs_produced)" 0 thread_jobs_produced
require_equal "$(number_field "$thread_row" jobs_consumed)" 0 thread_jobs_consumed

if "$hplf_binary" --workers 0 >/dev/null 2>&1; then
    echo "zero workers unexpectedly accepted" >&2
    exit 1
fi

echo "worker-pool validation mode=$mode checksum=$multi_checksum"
