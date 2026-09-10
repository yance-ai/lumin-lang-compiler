#!/usr/bin/env python3
"""
Lumyr 模糊测试工具
用法: python fuzz_runner.py [--count N] [--seed S] [--vm-only] [--compile-only]
"""
import os
import sys
import random
import subprocess
import argparse
import time
from pathlib import Path

FUZZ_DIR = Path(__file__).parent
COMPILER = FUZZ_DIR.parent / "bin" / "lumyr.exe"
CASES_DIR = FUZZ_DIR / "cases"
CRASHES_DIR = FUZZ_DIR / "crashes"

# 随机程序生成器
class LumyrGenerator:
    def __init__(self, seed=None):
        self.rng = random.Random(seed)
        self.var_count = 0

    def new_var(self):
        self.var_count += 1
        return f"v{self.var_count}"

    def gen_expr(self, depth=0):
        if depth > 3:
            return self.gen_literal()
        kind = self.rng.choice(["literal", "var", "binop", "unary", "call", "index"])
        if kind == "literal":
            return self.gen_literal()
        elif kind == "var":
            return self.rng.choice(["x", "y", "z", "i", "n", "s", "arr", "m"])
        elif kind == "binop":
            op = self.rng.choice(["+", "-", "*", "/", "%", "==", "!=", "<", ">", "<=", ">=", "&&", "||"])
            return f"({self.gen_expr(depth+1)} {op} {self.gen_expr(depth+1)})"
        elif kind == "unary":
            return f"(!{self.gen_expr(depth+1)})"
        elif kind == "call":
            func = self.rng.choice(["len", "abs", "sqrt", "floor", "ceil", "type", "str"])
            return f"{func}({self.gen_expr(depth+1)})"
        elif kind == "index":
            return f"arr[{self.gen_expr(depth+1)}]"
        return self.gen_literal()

    def gen_literal(self):
        kind = self.rng.choice(["int", "float", "bool", "string", "none"])
        if kind == "int":
            return str(self.rng.randint(-1000, 1000))
        elif kind == "float":
            return f"{self.rng.uniform(-100, 100):.2f}"
        elif kind == "bool":
            return self.rng.choice(["true", "false"])
        elif kind == "string":
            length = self.rng.randint(0, 10)
            chars = "abcdefghijklmnopqrstuvwxyz0123456789 \t"
            return '"' + ''.join(self.rng.choice(chars) for _ in range(length)) + '"'
        elif kind == "none":
            return "none"
        return "0"

    def gen_stmt(self, depth=0):
        if depth > 2:
            return self.gen_simple_stmt()
        kind = self.rng.choice(["assign", "print", "if", "for", "while", "block", "simple"])
        if kind == "assign":
            var = self.rng.choice(["x", "y", "z", "i", "n", "s"])
            return f"{var} = {self.gen_expr()};"
        elif kind == "print":
            return f"print({self.gen_expr()});"
        elif kind == "if":
            cond = self.gen_expr()
            then_body = self.gen_stmt(depth+1)
            if self.rng.random() < 0.3:
                else_body = self.gen_stmt(depth+1)
                return f"if ({cond}) {{ {then_body} }} else {{ {else_body} }}"
            return f"if ({cond}) {{ {then_body} }}"
        elif kind == "for":
            var = self.rng.choice(["i", "j", "k"])
            limit = self.rng.randint(0, 20)
            body = self.gen_stmt(depth+1)
            return f"for ({var} = 0; {var} < {limit}; {var} = {var} + 1) {{ {body} }}"
        elif kind == "while":
            cond = self.gen_expr()
            body = self.gen_stmt(depth+1)
            return f"while ({cond}) {{ {body} }}"
        elif kind == "block":
            stmts = [self.gen_simple_stmt() for _ in range(self.rng.randint(1, 3))]
            return "{ " + " ".join(stmts) + " }"
        else:
            return self.gen_simple_stmt()

    def gen_simple_stmt(self):
        kind = self.rng.choice(["assign", "print", "break", "continue", "pass"])
        if kind == "assign":
            var = self.rng.choice(["x", "y", "z", "i", "n", "s"])
            return f"{var} = {self.gen_expr()};"
        elif kind == "print":
            return f"print({self.gen_expr()});"
        elif kind == "break":
            return "break;"
        elif kind == "continue":
            return "continue;"
        else:
            return "x = x + 1;"

    def generate(self, num_stmts=20):
        self.var_count = 0
        stmts = []
        # 初始化变量
        stmts.append("x = 0; y = 1; z = 2; n = 10; s = \"hello\";")
        stmts.append("arr = [1, 2, 3, 4, 5];")
        stmts.append("m = {\"a\": 1, \"b\": 2};")
        for _ in range(num_stmts):
            stmts.append(self.gen_stmt())
        return "\n".join(stmts) + "\n"

def run_vm(code, timeout=10):
    """VM 模式运行"""
    case_file = CASES_DIR / "_fuzz_tmp.lm"
    case_file.write_text(code, encoding='utf-8')
    try:
        result = subprocess.run(
            [str(COMPILER), str(case_file)],
            capture_output=True, text=True, timeout=timeout,
            cwd=str(FUZZ_DIR.parent)
        )
        return result.stdout, result.stderr, result.returncode
    except subprocess.TimeoutExpired:
        return "", "TIMEOUT", -1
    except Exception as e:
        return "", str(e), -1
    finally:
        if case_file.exists():
            case_file.unlink()

def run_compile(code, timeout=30):
    """编译模式运行"""
    import tempfile
    case_file = CASES_DIR / "_fuzz_tmp.lm"
    case_file.write_text(code, encoding='utf-8')
    tmp_exe = CASES_DIR / "_fuzz_tmp"
    try:
        # 编译
        result = subprocess.run(
            [str(COMPILER), "-c", str(case_file), "-o", str(tmp_exe)],
            capture_output=True, text=True, timeout=timeout,
            cwd=str(FUZZ_DIR.parent)
        )
        if result.returncode != 0:
            return "", result.stderr, result.returncode
        # 运行
        exe_path = str(tmp_exe) + ".exe"
        if not os.path.exists(exe_path):
            exe_path = str(tmp_exe)
        result = subprocess.run(
            [exe_path],
            capture_output=True, text=True, timeout=timeout,
            cwd=str(FUZZ_DIR.parent)
        )
        return result.stdout, result.stderr, result.returncode
    except subprocess.TimeoutExpired:
        return "", "TIMEOUT", -1
    except Exception as e:
        return "", str(e), -1
    finally:
        if case_file.exists():
            case_file.unlink()
        for ext in [".exe", ".c", ""]:
            p = str(tmp_exe) + ext
            if os.path.exists(p):
                try: os.remove(p)
                except: pass

def save_crash(code, vm_out, vm_err, vm_rc, comp_out, comp_err, comp_rc, name):
    """保存崩溃用例"""
    crash_file = CRASHES_DIR / f"{name}.lm"
    crash_file.write_text(code, encoding='utf-8')
    info_file = CRASHES_DIR / f"{name}.txt"
    with open(info_file, 'w', encoding='utf-8') as f:
        f.write(f"VM returncode: {vm_rc}\n")
        f.write(f"VM stdout:\n{vm_out}\n")
        f.write(f"VM stderr:\n{vm_err}\n")
        f.write(f"\nCompile returncode: {comp_rc}\n")
        f.write(f"Compile stdout:\n{comp_out}\n")
        f.write(f"Compile stderr:\n{comp_err}\n")
    print(f"  CRASH saved: {crash_file}")

def main():
    parser = argparse.ArgumentParser(description="Lumyr 模糊测试")
    parser.add_argument("--count", type=int, default=100, help="测试用例数量")
    parser.add_argument("--seed", type=int, default=None, help="随机种子")
    parser.add_argument("--vm-only", action="store_true", help="只测试 VM 模式")
    parser.add_argument("--compile-only", action="store_true", help="只测试编译模式")
    args = parser.parse_args()

    CASES_DIR.mkdir(exist_ok=True)
    CRASHES_DIR.mkdir(exist_ok=True)

    gen = LumyrGenerator(args.seed)
    crashes = 0
    diffs = 0

    print(f"=== Lumyr Fuzz Testing ===")
    print(f"Count: {args.count}, Seed: {args.seed}")
    print()

    for i in range(args.count):
        code = gen.generate(num_stmts=random.randint(10, 30))

        vm_out, vm_err, vm_rc = "", "", 0
        comp_out, comp_err, comp_rc = "", "", 0

        if not args.compile_only:
            vm_out, vm_err, vm_rc = run_vm(code)
        if not args.vm_only:
            comp_out, comp_err, comp_rc = run_compile(code)

        # 检测崩溃
        is_crash = False
        if vm_rc < 0 or (vm_rc != 0 and "error" not in vm_err.lower() and "syntax" not in vm_err.lower()):
            is_crash = True
        if comp_rc < 0 or (comp_rc != 0 and "error" not in comp_err.lower() and "syntax" not in comp_err.lower()):
            is_crash = True

        # 检测输出不一致（差分测试）
        is_diff = False
        if not args.vm_only and not args.compile_only:
            if vm_rc == 0 and comp_rc == 0 and vm_out != comp_out:
                is_diff = True

        if is_crash:
            crashes += 1
            save_crash(code, vm_out, vm_err, vm_rc, comp_out, comp_err, comp_rc, f"crash_{i:04d}")
        elif is_diff:
            diffs += 1
            save_crash(code, vm_out, vm_err, vm_rc, comp_out, comp_err, comp_rc, f"diff_{i:04d}")

        if (i + 1) % 10 == 0:
            print(f"  Progress: {i+1}/{args.count}  crashes={crashes}  diffs={diffs}")

    print()
    print(f"=== Fuzz Results ===")
    print(f"Total: {args.count}")
    print(f"Crashes: {crashes}")
    print(f"Output diffs: {diffs}")
    print(f"Crash cases saved to: {CRASHES_DIR}")

if __name__ == "__main__":
    main()
