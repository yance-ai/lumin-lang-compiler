#!/usr/bin/env python3
"""
Lumyr 性能基准测试运行器
用法: python run_benchmarks.py [--mode vm|compile|both] [--filter <pattern>]
"""
import os
import sys
import re
import time
import subprocess
import argparse
from pathlib import Path

BENCH_DIR = Path(__file__).parent
COMPILER = BENCH_DIR.parent / "bin" / "lumyr.exe"

def run_vm(bench_file):
    """VM 模式运行基准测试"""
    try:
        result = subprocess.run(
            [str(COMPILER), str(bench_file)],
            capture_output=True, text=True, timeout=120,
            cwd=str(BENCH_DIR.parent)
        )
        return result.stdout + result.stderr, result.returncode
    except subprocess.TimeoutExpired:
        return "TIMEOUT", -1
    except Exception as e:
        return f"ERROR: {e}", -1

def run_compile(bench_file):
    """编译模式运行基准测试"""
    import tempfile
    tmp_exe = BENCH_DIR.parent / f"_bench_{bench_file.stem}"
    try:
        # 编译
        result = subprocess.run(
            [str(COMPILER), "-c", str(bench_file), "-o", str(tmp_exe)],
            capture_output=True, text=True, timeout=120,
            cwd=str(BENCH_DIR.parent)
        )
        if result.returncode != 0:
            return f"COMPILE ERROR: {result.stderr}", result.returncode
        # 运行
        exe_path = str(tmp_exe) + ".exe"
        if not os.path.exists(exe_path):
            exe_path = str(tmp_exe)
        result = subprocess.run(
            [exe_path],
            capture_output=True, text=True, timeout=120,
            cwd=str(BENCH_DIR.parent)
        )
        return result.stdout + result.stderr, result.returncode
    except subprocess.TimeoutExpired:
        return "TIMEOUT", -1
    except Exception as e:
        return f"ERROR: {e}", -1
    finally:
        # 清理临时文件
        for ext in [".exe", ".c", ""]:
            p = str(tmp_exe) + ext
            if os.path.exists(p):
                try: os.remove(p)
                except: pass

def parse_time(output):
    """从输出中解析时间"""
    times = re.findall(r'time:\s*([\d.]+)s', output)
    return [float(t) for t in times]

def run_benchmarks(mode="both", filter_pattern=None):
    bench_files = sorted(BENCH_DIR.glob("bench_*.lm"))
    if filter_pattern:
        bench_files = [f for f in bench_files if filter_pattern in f.name]

    print("=" * 70)
    print(f"Lumyr 性能基准测试  (mode: {mode})")
    print("=" * 70)
    print()

    all_results = {}

    for bench_file in bench_files:
        name = bench_file.stem
        print(f"--- {name} ---")

        if mode in ("vm", "both"):
            t0 = time.time()
            output, rc = run_vm(bench_file)
            elapsed = time.time() - t0
            times = parse_time(output)
            status = "PASS" if rc == 0 else f"FAIL(rc={rc})"
            print(f"  VM:      {status}  wall={elapsed:.3f}s  inner={times}")
            if rc != 0:
                for line in output.split('\n')[:5]:
                    if line.strip(): print(f"           {line.strip()}")
            all_results[f"{name}_vm"] = times

        if mode in ("compile", "both"):
            t0 = time.time()
            output, rc = run_compile(bench_file)
            elapsed = time.time() - t0
            times = parse_time(output)
            status = "PASS" if rc == 0 else f"FAIL(rc={rc})"
            print(f"  Compile: {status}  wall={elapsed:.3f}s  inner={times}")
            if rc != 0:
                for line in output.split('\n')[:5]:
                    if line.strip(): print(f"           {line.strip()}")
            all_results[f"{name}_compile"] = times

        print()

    # 汇总
    print("=" * 70)
    print("汇总 (内部测试时间，秒)")
    print("=" * 70)
    for key, times in all_results.items():
        if times:
            avg = sum(times) / len(times)
            print(f"  {key:30s} avg={avg:.4f}s  items={times}")
    print()
    print("完成。")

if __name__ == "__main__":
    parser = argparse.ArgumentParser(description="Lumyr 性能基准测试")
    parser.add_argument("--mode", choices=["vm", "compile", "both"], default="both")
    parser.add_argument("--filter", default=None, help="只运行包含该模式的基准测试")
    args = parser.parse_args()
    run_benchmarks(args.mode, args.filter)
