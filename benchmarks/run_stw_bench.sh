#!/bin/bash
# ============================================================
# run_stw_bench.sh — 增量标记 STW 停顿对比测量
#
# 对 LUMIN_GC_INCREMENTAL=0（全量 STW）与 =1（增量标记）各跑一次，
# 解析 stderr 中的 [GC major]/[GC minor] 停顿（us），统计：
#   - Major 初始 STW / 最终 STW 的 P50/P99/max
#   - Minor STW 的 P50/P99/max
#   - 每场景累计 STW（来自 stdout SCENARIO 行）
# ============================================================
set -u
cd "$(dirname "$0")/.."
BIN=./bin/lumin

run_one() {
    local mode="$1"   # "0" or "1"
    local out="/tmp/stw_inc_${mode}.out"
    local err="/tmp/stw_inc_${mode}.err"
    LUMIN_GC_INCREMENTAL="$mode" LUMIN_GC_STATS=1 "$BIN" benchmarks/gc_stw_bench.lm >"$out" 2>"$err"
    echo "$out" "$err"
}

# 从 stderr 提取所有单次 STW 停顿（us），写入临时文件
extract_pauses() {
    local err="$1"
    local out="$2"
    # major: 取 initial STW 与 final STW 两个值
    grep "^\[GC major\]" "$err" | sed -E 's/.*initial STW=([0-9]+)us.*/\1/' > "$out.major_init"
    grep "^\[GC major\]" "$err" | sed -E 's/.*final STW=([0-9]+)us.*/\1/' >> "$out.major_init"
    # minor: 取 STW= 值
    grep "^\[GC minor\]" "$err" | sed -E 's/.*STW=([0-9]+)us.*/\1/' > "$out.minor"
}

# 统计中位数/分位/最大值（输入为每行一个整数 us）
stats() {
    local f="$1"
    if [ ! -s "$f" ]; then echo "n=0"; return; fi
    local n=$(wc -l < "$f" | tr -d ' ')
    # 排序
    sort -n "$f" > "$f.sorted"
    local max=$(tail -1 "$f.sorted")
    local p50=$(awk -v n="$n" 'NR==int(n*0.5){print; exit}' "$f.sorted")
    local p99=$(awk -v n="$n" 'NR==int(n*0.99){print; exit}' "$f.sorted")
    local sum=$(awk '{s+=$1} END{print s}' "$f.sorted")
    echo "n=$n p50=${p50}us p99=${p99}us max=${max}us sum=${sum}us"
}

echo "============================================================"
echo "  GC 增量标记 STW 停顿对比"
echo "============================================================"

for MODE in 0 1; do
    read OUT ERR < <(run_one "$MODE")
    LABEL=$([ "$MODE" = "1" ] && echo "增量标记 ON" || echo "全量 STW (增量 OFF)")
    echo ""
    echo "########## MODE: $LABEL  (LUMIN_GC_INCREMENTAL=$MODE) ##########"
    echo "--- 场景累计 STW ---"
    grep "^SCENARIO\|^BENCH DONE" "$OUT"
    echo "--- Major GC 单次停顿 (初始+最终, us) ---"
    extract_pauses "$ERR" "/tmp/pauses_$MODE"
    stats "/tmp/pauses_$MODE.major_init"
    echo "--- Minor GC 单次停顿 (us) ---"
    stats "/tmp/pauses_$MODE.minor"
done

echo ""
echo "============================================================"
echo "  测量完成。注意：增量 ON 时 Major 的并发标记阶段不计入 STW，"
echo "  应用线程在标记期间可继续运行（仅写屏障有少量开销）。"
echo "============================================================"
