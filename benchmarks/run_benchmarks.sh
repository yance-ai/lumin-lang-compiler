#!/bin/bash
# ============================================================
# run_benchmarks.sh — 性能基准测量脚本
# 对每个基准在两个版本上各跑 3 次，收集 time/gc_count/maxRSS
# ============================================================

OPT_DIR="/Users/kai/Documents/yangchuan/lumin-lang/lumin-lang-compiler"
BASE_DIR="/tmp/lumin-baseline"
BENCHES="bench_string bench_array bench_map bench_mixed bench_gc_pressure"

echo "============================================================"
echo "  Lumin-Lang Compiler Performance Benchmark Suite"
echo "  Optimized: HEAD (3ad1eff) | Baseline: ed15d49"
echo "============================================================"
echo ""

for VERSION in "optimized:$OPT_DIR" "baseline:$BASE_DIR"; do
    VNAME="${VERSION%%:*}"
    VDIR="${VERSION##*:}"
    echo "########## VERSION: $VNAME ($VDIR) ##########"
    echo ""

    for BENCH in $BENCHES; do
        echo "------ $BENCH ------"
        if "$VDIR/bin/lumin" -c "$VDIR/benchmarks/$BENCH.lm" -o "/tmp/${VNAME}_${BENCH}" 2>/dev/null; then
            echo "compiled OK"
        else
            echo "COMPILE FAILED"
            continue
        fi

        for RUN in 1 2 3; do
            # /usr/bin/time -l writes to stderr; program writes to stdout
            # macOS format: "4.43 real  1.80 user  0.19 sys"
            TIME_OUT=$(/usr/bin/time -l "/tmp/${VNAME}_${BENCH}" 2>&1 >/tmp/${VNAME}_${BENCH}_out.txt)
            PROG_OUT=$(cat /tmp/${VNAME}_${BENCH}_out.txt)

            REAL=$(echo "$TIME_OUT" | grep "real" | head -1 | awk '{print $1}')
            MAXRSS=$(echo "$TIME_OUT" | grep "maximum resident set size" | awk '{print $1}')
            GC_DELTA=$(echo "$PROG_OUT" | grep "gc_delta=" | cut -d= -f2)
            RESULT=$(echo "$PROG_OUT" | grep "result=" | cut -d= -f2)

            echo "  run $RUN: real=${REAL}s  maxRSS=${MAXRSS}  gc_delta=${GC_DELTA}  result=${RESULT}"
        done
        echo ""
    done
done

echo "============================================================"
echo "  All benchmarks complete."
echo "============================================================"
