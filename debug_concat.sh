#!/bin/bash
set -x

SRC_ROOT="src"
MANIFEST="runtime_manifest.txt"
OUT_H="${SRC_ROOT}/runtime-full/runtime_full.h"

mkdir -p "${SRC_ROOT}/runtime-full"
> "$OUT_H"

echo "=== 读取 manifest 第一行 ==="
while IFS= read -r line; do
    echo "manifest line: [$line]"
    [[ -z "$line" || "$line" =~ ^# ]] && continue
    if [[ "$line" == *.h ]]; then
        full="${SRC_ROOT}/${line}"
        echo "full path: [$full]"
        echo "file exists: $(test -f "$full" && echo yes || echo no)"
        fdir=$(dirname -- "$full")
        echo "fdir: [$fdir]"
        
        # 手动读取文件内容
        echo "=== 文件前5行 ==="
        head -5 "$full"
        
        # 手动 process_header
        echo "=== 开始 process_header ==="
        while IFS= read -r hline || [[ -n "$hline" ]]; do
            echo "hline: [$hline]"
            if [[ "$hline" =~ ^[[:space:]]*#include[[:space:]]*\<(.*)\> ]]; then
                echo "  -> 系统头文件: ${BASH_REMATCH[1]}"
            elif [[ "$hline" =~ ^[[:space:]]*#include[[:space:]]*\"(.*)\" ]]; then
                echo "  -> 本地头文件: ${BASH_REMATCH[1]}"
            else
                echo "  -> 普通行"
            fi
        done < "$full"
        break
    fi
done < "$MANIFEST"

echo "=== OUT_H 内容 ==="
cat "$OUT_H"
echo "=== 完成 ==="
