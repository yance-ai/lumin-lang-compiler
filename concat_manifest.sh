#!/bin/bash
set -e

SRC_ROOT="src"
MANIFEST="runtime_manifest.txt"

INCLUDED_H=()   # 头文件去重
COLLECT_C=()    # 收集需要导出的c文件路径（只收集，不写文件）
SYS_INC=()      # 系统头文件 <>

OUT_H="${SRC_ROOT}/runtime-full/runtime_full.h"
OUT_C="${SRC_ROOT}/runtime-full/runtime_full.c"

mkdir -p "${SRC_ROOT}/runtime-full"

# 解析include路径：current_base 当前文件目录，inc_rel include后的字符串
resolve_include() {
    local current_base="$1"
    local inc_rel="$2"

    local cand="${current_base}/${inc_rel}"
    if [[ -f "$cand" ]]; then
        echo "$cand"
        return 0
    fi

    local INCLUDE_DIRS=(
        "${SRC_ROOT}"
        "${SRC_ROOT}/runtime"
        "${SRC_ROOT}/ast"
    )
    for idir in "${INCLUDE_DIRS[@]}"; do
        cand="${idir}/${inc_rel}"
        if [[ -f "$cand" ]]; then
            echo "$cand"
            return 0
        fi
    done

    echo >&2 "ERROR: cannot find include '$inc_rel' (base: $current_base)"
    exit 1
}

# 输出c文件内容到OUT_C，过滤 #include "xxx.h"，保留系统头
dump_c_file() {
    local f="$1"
    while IFS= read -r cline || [[ -n "$cline" ]]; do
        if [[ "$cline" =~ ^[[:space:]]*#include[[:space:]]*\" ]]; then
            continue
        fi
        echo "$cline" >> "$OUT_C"
    done < "$f"
    echo "" >> "$OUT_C"
}

# process_header：只展开头文件、收集c路径，不写c输出
process_header() {
    local h_path="$1"
    local base_dir="$2"

    # 头文件去重
    for p in "${INCLUDED_H[@]}"; do
        if [[ "$p" == "$h_path" ]]; then
            return
        fi
    done
    INCLUDED_H+=("$h_path")

    local line
    while IFS= read -r line || [[ -n "$line" ]]; do
        if [[ "$line" =~ ^[[:space:]]*#include[[:space:]]*\<(.*)\> ]]; then
            local sysh="${BASH_REMATCH[1]}"
            local has=0
            for s in "${SYS_INC[@]}"; do
                if [[ "$s" == "$sysh" ]]; then
                    has=1
                    break
                fi
            done
            if [[ $has -eq 0 ]]; then
                SYS_INC+=("$sysh")
                echo "$line" >> "$OUT_H"
            fi
        elif [[ "$line" =~ ^[[:space:]]*#include[[:space:]]*\"(.*)\" ]]; then
            local inc_rel="${BASH_REMATCH[1]}"
            local inc_h
            inc_h=$(resolve_include "$base_dir" "$inc_rel")
            local inc_h_dir=$(dirname -- "$inc_h")
            process_header "$inc_h" "$inc_h_dir"
        else
            echo "$line" >> "$OUT_H"
        fi
    done < "$h_path"
    echo "" >> "$OUT_H"

    # 只收集c文件路径，不dump
    local h_name="${h_path##*/}"
    local h_root="${h_name%.h}"
    local c_candidate="${base_dir}/${h_root}.c"
    if [[ -f "$c_candidate" ]]; then
        local already=0
        for cc in "${COLLECT_C[@]}"; do
            if [[ "$cc" == "$c_candidate" ]]; then
                already=1
                break
            fi
        done
        if [[ $already -eq 0 ]]; then
            COLLECT_C+=("$c_candidate")
        fi
    fi
}

# ===================== 生成 runtime_full.h =====================
> "$OUT_H"
INCLUDED_H=()
COLLECT_C=()
SYS_INC=()

while IFS= read -r line; do
    [[ -z "$line" || "$line" =~ ^# ]] && continue
    if [[ "$line" == *.h ]]; then
        full="${SRC_ROOT}/${line}"
        fdir=$(dirname -- "$full")
        process_header "$full" "$fdir"
    fi
done < "$MANIFEST"

# ===================== 生成 runtime_full.c =====================
> "$OUT_C"
cat > "$OUT_C" <<'EOF'
/* auto‑generated runtime single‑file */

EOF

# 第一步：输出头文件收集到的c文件
for cfile in "${COLLECT_C[@]}"; do
    dump_c_file "$cfile"
done

# 第二步：处理manifest显式列出的c文件，去重（避开已经COLLECT_C里的）
while IFS= read -r line; do
    [[ -z "$line" || "$line" =~ ^# ]] && continue
    if [[ "$line" == *.c ]]; then
        full="${SRC_ROOT}/${line}"
        skip=0
        for cc in "${COLLECT_C[@]}"; do
            if [[ "$cc" == "$full" ]]; then
                skip=1
                break
            fi
        done
        if [[ $skip -eq 0 ]]; then
            dump_c_file "$full"
        fi
    fi
done < "$MANIFEST"

echo "Generated: $OUT_H $OUT_C"
