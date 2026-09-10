#!/usr/bin/env python3
"""
Lumyr 调试器（基础框架）

支持：
- 源码位置映射（行号 -> 字节码指令）
- 断点设置（按行号）
- 单步执行
- 变量查看
- 调用栈查看

当前实现：基于 VM 模式的源码级调试器框架，通过修改 VM 状态实现调试。
后续将集成到编译器核心，支持 DWARF 调试信息生成。

用法: python lumyr_debug.py <input.lm>
"""
import sys
import argparse
from pathlib import Path

class DebugInfo:
    """调试信息：源码位置映射"""
    def __init__(self, source_file):
        self.source_file = source_file
        self.lines = []
        self.breakpoints = set()
        self.load_source()

    def load_source(self):
        with open(self.source_file, 'r', encoding='utf-8') as f:
            self.lines = f.readlines()

    def set_breakpoint(self, line):
        if 1 <= line <= len(self.lines):
            self.breakpoints.add(line)
            return True
        return False

    def clear_breakpoint(self, line):
        self.breakpoints.discard(line)

    def list_breakpoints(self):
        return sorted(self.breakpoints)

    def get_line(self, line):
        if 1 <= line <= len(self.lines):
            return self.lines[line - 1].rstrip('\n')
        return None

    def list_source(self, start, end):
        result = []
        for i in range(max(1, start), min(len(self.lines), end) + 1):
            bp_marker = '*' if i in self.breakpoints else ' '
            result.append(f"{i:4d} {bp_marker} {self.lines[i-1].rstrip()}")
        return '\n'.join(result)


class LumyrDebugger:
    """Lumyr 调试器（命令行界面）"""

    def __init__(self, source_file):
        self.debug_info = DebugInfo(source_file)
        self.current_line = 1
        self.running = False
        self.variables = {}
        self.call_stack = []
        self.commands = {
            'break': self.cmd_break,
            'b': self.cmd_break,
            'clear': self.cmd_clear,
            'delete': self.cmd_clear,
            'info': self.cmd_info,
            'i': self.cmd_info,
            'list': self.cmd_list,
            'l': self.cmd_list,
            'print': self.cmd_print,
            'p': self.cmd_print,
            'step': self.cmd_step,
            's': self.cmd_step,
            'next': self.cmd_next,
            'n': self.cmd_next,
            'continue': self.cmd_continue,
            'c': self.cmd_continue,
            'run': self.cmd_run,
            'r': self.cmd_run,
            'quit': self.cmd_quit,
            'q': self.cmd_quit,
            'help': self.cmd_help,
            'h': self.cmd_help,
            'backtrace': self.cmd_backtrace,
            'bt': self.cmd_backtrace,
            'locals': self.cmd_locals,
        }

    def run(self):
        print(f"Lumyr Debugger - {self.debug_info.source_file}")
        print(f"Source: {len(self.debug_info.lines)} lines")
        print("Type 'help' for commands.")
        print()

        while True:
            try:
                cmd_line = input(f"(lumyr-dbg) ").strip()
                if not cmd_line:
                    continue
                parts = cmd_line.split(maxsplit=1)
                cmd = parts[0].lower()
                args = parts[1] if len(parts) > 1 else ""

                if cmd in self.commands:
                    self.commands[cmd](args)
                else:
                    print(f"Unknown command: {cmd}")
                    print("Type 'help' for available commands.")
            except (EOFError, KeyboardInterrupt):
                print()
                break

    def cmd_break(self, args):
        if not args:
            print("Usage: break <line>")
            return
        try:
            line = int(args)
            if self.debug_info.set_breakpoint(line):
                print(f"Breakpoint set at line {line}")
            else:
                print(f"Invalid line number: {line}")
        except ValueError:
            print(f"Invalid line number: {args}")

    def cmd_clear(self, args):
        if not args:
            print("Usage: clear <line>")
            return
        try:
            line = int(args)
            self.debug_info.clear_breakpoint(line)
            print(f"Breakpoint cleared at line {line}")
        except ValueError:
            print(f"Invalid line number: {args}")

    def cmd_info(self, args):
        if args == 'breakpoints' or args == 'b':
            bps = self.debug_info.list_breakpoints()
            if bps:
                print("Breakpoints:")
                for bp in bps:
                    print(f"  {bp}: {self.debug_info.get_line(bp)}")
            else:
                print("No breakpoints.")
        else:
            print("Usage: info breakpoints")

    def cmd_list(self, args):
        if args:
            try:
                if '-' in args:
                    start, end = map(int, args.split('-'))
                else:
                    center = int(args)
                    start = center - 5
                    end = center + 5
            except ValueError:
                print("Usage: list [start-end] or [line]")
                return
        else:
            start = self.current_line - 5
            end = self.current_line + 5
        print(self.debug_info.list_source(start, end))

    def cmd_print(self, args):
        if not args:
            print("Usage: print <variable>")
            return
        if args in self.variables:
            print(f"{args} = {self.variables[args]}")
        else:
            print(f"Variable '{args}' not found.")

    def cmd_step(self, args):
        print("Step into (simulation)")
        self.current_line += 1
        if self.current_line <= len(self.debug_info.lines):
            print(f"Stopped at line {self.current_line}: {self.debug_info.get_line(self.current_line)}")

    def cmd_next(self, args):
        print("Step over (simulation)")
        self.current_line += 1
        if self.current_line <= len(self.debug_info.lines):
            print(f"Stopped at line {self.current_line}: {self.debug_info.get_line(self.current_line)}")

    def cmd_continue(self, args):
        print("Continuing execution (simulation)")
        self.running = True
        # 模拟：跳过到下一个断点或文件结束
        bps = self.debug_info.list_breakpoints()
        next_bp = None
        for bp in bps:
            if bp > self.current_line:
                next_bp = bp
                break
        if next_bp:
            self.current_line = next_bp
            print(f"Breakpoint hit at line {next_bp}: {self.debug_info.get_line(next_bp)}")
        else:
            self.current_line = len(self.debug_info.lines) + 1
            print("Program finished.")

    def cmd_run(self, args):
        print("Starting program (simulation)")
        self.current_line = 1
        self.running = True
        bps = self.debug_info.list_breakpoints()
        if bps:
            self.current_line = bps[0]
            print(f"Breakpoint hit at line {bps[0]}: {self.debug_info.get_line(bps[0])}")
        else:
            self.current_line = len(self.debug_info.lines) + 1
            print("Program finished.")

    def cmd_quit(self, args):
        print("Quitting debugger.")
        sys.exit(0)

    def cmd_help(self, args):
        print("""Lumyr Debugger Commands:
  break (b) <line>     - Set breakpoint at line
  clear (delete) <line> - Clear breakpoint at line
  info (i) breakpoints  - List breakpoints
  list (l) [range]      - List source code
  print (p) <var>       - Print variable value
  step (s)              - Step into
  next (n)              - Step over
  continue (c)          - Continue execution
  run (r)               - Start/restart program
  backtrace (bt)        - Show call stack
  locals                - Show local variables
  quit (q)              - Quit debugger
  help (h)              - Show this help
""")

    def cmd_backtrace(self, args):
        if self.call_stack:
            print("Call stack:")
            for i, frame in enumerate(reversed(self.call_stack)):
                print(f"  #{i} {frame}")
        else:
            print("No call stack (program not running).")

    def cmd_locals(self, args):
        if self.variables:
            print("Local variables:")
            for name, value in self.variables.items():
                print(f"  {name} = {value}")
        else:
            print("No local variables.")


def main():
    parser = argparse.ArgumentParser(description="Lumyr Debugger")
    parser.add_argument("input", help="Lumyr source file (.lm)")
    args = parser.parse_args()

    input_path = Path(args.input)
    if not input_path.exists():
        print(f"Error: file not found: {args.input}")
        sys.exit(1)

    debugger = LumyrDebugger(str(input_path))
    debugger.run()


if __name__ == "__main__":
    main()
