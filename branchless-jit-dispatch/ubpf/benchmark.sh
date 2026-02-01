#!/bin/bash
# Usage: ./benchmark.sh <baseline_bin> <mitigated_bin> <bpf_dir> [iterations]
set -e

BASELINE=$1
MITIGATED=$2
BPF_DIR=$3
ITER=${4:-50}

bench() {
    local bin=$1 prog=$2 mem=$3
    local start=$(date +%s%N)
    for i in $(seq 1 $ITER); do
        if [ -n "$mem" ]; then $bin -m $mem $prog >/dev/null 2>&1
        else $bin $prog >/dev/null 2>&1; fi
    done
    echo $(( ($(date +%s%N) - start) / 1000000 ))
}

echo "program,baseline_ms,mitigated_ms,overhead"
for prog in simple prime; do
    t1=$(bench $BASELINE $BPF_DIR/${prog}.bpf.bin "")
    t2=$(bench $MITIGATED $BPF_DIR/${prog}.bpf.bin "")
    echo "$prog,$t1,$t2,$(echo "scale=2; $t2/$t1" | bc)x"
done
for prog in memory_a_plus_b log2_int switch memcpy strcmp_full; do
    [ -f $BPF_DIR/${prog}.mem ] || continue
    t1=$(bench $BASELINE $BPF_DIR/${prog}.bpf.bin $BPF_DIR/${prog}.mem)
    t2=$(bench $MITIGATED $BPF_DIR/${prog}.bpf.bin $BPF_DIR/${prog}.mem)
    echo "$prog,$t1,$t2,$(echo "scale=2; $t2/$t1" | bc)x"
done
