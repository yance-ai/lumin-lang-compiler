#!/usr/bin/env bash
# 双通道回归：对 tests/*.lm 逐个跑 VM 与编译通道，比对 stdout。
cd "$(dirname "$0")" || exit 1
FAIL=0; PASS=0; SKIP=0
for f in tests/*.lm; do
  base=$(basename "$f")
  case "$base" in
    requests_test.lm|crypto_enc_test.lm|thread_stress.lm|thread_test.lm|thread_container_test.lm|gc_thread_stress.lm|json_type_thread_test.lm|cond_timeout_test.lm|lock_test.lm|lock_test2.lm|threadlocal_test.lm|gc_return_race.lm|gc_promotion_test.lm|gc_efficiency_diag.lm|mem_leak_test.lm)
      SKIP=$((SKIP+1)); echo "SKIP  $base"; continue;;
  esac
  vmo=$(mktemp); cmo=$(mktemp)
  ./bin/lumin "$f" 2>/dev/null > "$vmo"
  vrc=$?
  if [ $vrc -ne 0 ]; then echo "VM-FAIL $base (rc=$vrc)"; FAIL=$((FAIL+1)); rm -f "$vmo" "$cmo"; continue; fi
  ./bin/lumin -c "$f" -o /tmp/reg_bin_$$ 2>/dev/null
  if [ $? -ne 0 ]; then echo "CC-GEN-FAIL $base"; FAIL=$((FAIL+1)); rm -f "$vmo" "$cmo"; continue; fi
  /tmp/reg_bin_$$ 2>/dev/null > "$cmo"
  crc=$?
  rm -f /tmp/reg_bin_$$
  if [ $crc -ne 0 ]; then echo "CC-RUN-FAIL $base (rc=$crc)"; FAIL=$((FAIL+1)); rm -f "$vmo" "$cmo"; continue; fi
  if diff -q "$vmo" "$cmo" >/dev/null; then PASS=$((PASS+1)); else
    echo "DIFF  $base"; diff "$vmo" "$cmo" | head -8; FAIL=$((FAIL+1)); fi
  rm -f "$vmo" "$cmo"
done
echo "=== pass=$PASS fail=$FAIL skip=$SKIP ==="
