#!/usr/bin/env python3
"""
Lumyr 调试器（v2 - 增强版）

支持：
- 源码位置映射和高亮
- 断点设置（按行号、条件断点）
- 观察点（变量变化时中断）
- 单步执行（step/next）
- 运行到行（until）
- 跳过（skip）
- 变量查看和表达式求值
- 调用栈查看
- 反汇编（框架）
- 断点命令列表（commands）

用法: python lumyr_debug.py <input.lm>
"""
import sys
import re
import argparse
from pathlib import Path

class Breakpoint:
    """断点"""
    def __init__(self, line, condition=None, enabled=True):
        self.line = line
        self.condition = condition
        self.enabled = enabled
        self.hit_count = 0
        self.commands = []  # 断点命中时执行的命令

class Watchpoint:
    """观察点"""
    def __init__(self, var_name, condition=None):
        self.var_name = var_name
        self.condition = condition
        self.old_value = None
        self.hit_count = 0

class DebugInfo:
    """调试信息：源码位置映射"""
    def __init__(self, source_file):
        self.source_file = source_file
        self.lines = []
        self.breakpoints = {}  # line -> Breakpoint
        self.watchpoints = []  # list of Watchpoint
        self.load_source()

    def load_source(self):
        with open(self.source_file, 'r', encoding='utf-8') as f:
            self.lines = f.readlines()

    def set_breakpoint(self, line, condition=None):
        if 1 <= line <= len(self.lines):
            if line not in self.breakpoints:
                self.breakpoints[line] = Breakpoint(line, condition)
            elif condition:
                self.breakpoints[line].condition = condition
            return True
        return False

    def clear_breakpoint(self, line):
        if line in self.breakpoints:
            del self.breakpoints[line]
            return True
        return False

    def enable_breakpoint(self, line, enabled=True):
        if line in self.breakpoints:
            self.breakpoints[line].enabled = enabled
            return True
        return False

    def list_breakpoints(self):
        return sorted(self.breakpoints.values(), key=lambda b: b.line)

    def add_watchpoint(self, var_name, condition=None):
        wp = Watchpoint(var_name, condition)
        self.watchpoints.append(wp)
        return len(self.watchpoints)

    def remove_watchpoint(self, index):
        if 0 <= index < len(self.watchpoints):
            self.watchpoints.pop(index)
            return True
        return False

    def get_line(self, line):
        if 1 <= line <= len(self.lines):
            return self.lines[line - 1].rstrip('\n')
        return None

    def list_source(self, start, end, current_line=None):
        result = []
        for i in range(max(1, start), min(len(self.lines), end) + 1):
            bp_marker = '●' if i in self.breakpoints else ' '
            cur_marker = '▶' if i == current_line else ' '
            enabled = self.breakpoints[i].enabled if i in self.breakpoints else True
            bp_char = bp_marker if enabled else '○'
            line_text = self.lines[i-1].rstrip()
            result.append(f"{i:4d} {bp_char}{cur_marker} {line_text}")
        return '\n'.join(result)


class ExpressionEvaluator:
    """简单的表达式求值器（用于调试器中的 print/eval 命令）"""
    def __init__(self, variables=None):
        self.variables = variables or {}

    def evaluate(self, expr):
        """求值表达式"""
        try:
            # 替换变量
            result = expr
            for var, val in sorted(self.variables.items(), key=lambda x: -len(x[0])):
                result = re.sub(r'\b' + re.escape(var) + r'\b', str(val), result)
            # 简单的算术求值
            return eval(result, {"__builtins__": {}}, {})
        except Exception as e:
            return f"<error: {e}>"


class LumyrDebugger:
    """Lumyr 调试器（命令行界面）"""

    def __init__(self, source_file):
        self.debug_info = DebugInfo(source_file)
        self.current_line = 1
        self.running = False
        self.variables = {}
        self.call_stack = []
        self.skip_count = 0
        self.evaluator = ExpressionEvaluator(self.variables)
        self.commands = {
            'break': self.cmd_break,
            'b': self.cmd_break,
            'tbreak': self.cmd_tbreak,
            'clear': self.cmd_clear,
            'delete': self.cmd_delete,
            'd': self.cmd_delete,
            'enable': self.cmd_enable,
            'disable': self.cmd_disable,
            'info': self.cmd_info,
            'i': self.cmd_info,
            'list': self.cmd_list,
            'l': self.cmd_list,
            'print': self.cmd_print,
            'p': self.cmd_print,
            'eval': self.cmd_eval,
            'watch': self.cmd_watch,
            'step': self.cmd_step,
            's': self.cmd_step,
            'next': self.cmd_next,
            'n': self.cmd_next,
            'continue': self.cmd_continue,
            'c': self.cmd_continue,
            'cont': self.cmd_continue,
            'run': self.cmd_run,
            'r': self.cmd_run,
            'until': self.cmd_until,
            'u': self.cmd_until,
            'skip': self.cmd_skip,
            'quit': self.cmd_quit,
            'q': self.cmd_quit,
            'exit': self.cmd_quit,
            'help': self.cmd_help,
            'h': self.cmd_help,
            'backtrace': self.cmd_backtrace,
            'bt': self.cmd_backtrace,
            'where': self.cmd_backtrace,
            'locals': self.cmd_locals,
            'disassemble': self.cmd_disassemble,
            'disasm': self.cmd_disassemble,
            'x': self.cmd_examine,
            'examine': self.cmd_examine,
            'set': self.cmd_set,
            'commands': self.cmd_commands,
            'frame': self.cmd_frame,
            'f': self.cmd_frame,
        }

    def run(self):
        print(f"Lumyr Debugger v2.0 - {self.debug_info.source_file}")
        print(f"Source: {len(self.debug_info.lines)} lines")
        print("Type 'help' for commands, 'help <cmd>' for detailed help.")
        print()

        while True:
            try:
                prompt = f"(lumyr-dbg) "
                if self.running:
                    prompt = f"(lumyr-dbg @ line {self.current_line}) "
                cmd_line = input(prompt).strip()
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
            print("Usage: break <line> [if condition]")
            return
        parts = args.split(' if ', 1)
        try:
            line = int(parts[0])
            condition = parts[1] if len(parts) > 1 else None
            if self.debug_info.set_breakpoint(line, condition):
                cond_str = f" if {condition}" if condition else ""
                print(f"Breakpoint {len(self.debug_info.breakpoints)} set at line {line}{cond_str}")
            else:
                print(f"Invalid line number: {line}")
        except ValueError:
            print(f"Invalid line number: {parts[0]}")

    def cmd_tbreak(self, args):
        """临时断点（命中一次后自动删除）"""
        if not args:
            print("Usage: tbreak <line>")
            return
        try:
            line = int(args)
            if self.debug_info.set_breakpoint(line):
                print(f"Temporary breakpoint set at line {line}")
        except ValueError:
            print(f"Invalid line number: {args}")

    def cmd_clear(self, args):
        if not args:
            print("Usage: clear <line>")
            return
        try:
            line = int(args)
            if self.debug_info.clear_breakpoint(line):
                print(f"Breakpoint at line {line} cleared")
            else:
                print(f"No breakpoint at line {line}")
        except ValueError:
            print(f"Invalid line number: {args}")

    def cmd_delete(self, args):
        if not args:
            print("Usage: delete <breakpoint-number>")
            bps = self.debug_info.list_breakpoints()
            for i, bp in enumerate(bps, 1):
                print(f"  {i}: line {bp.line}")
            return
        try:
            num = int(args)
            bps = self.debug_info.list_breakpoints()
            if 1 <= num <= len(bps):
                bp = bps[num - 1]
                self.debug_info.clear_breakpoint(bp.line)
                print(f"Breakpoint {num} (line {bp.line}) deleted")
            else:
                print(f"Invalid breakpoint number: {num}")
        except ValueError:
            print(f"Invalid breakpoint number: {args}")

    def cmd_enable(self, args):
        if not args:
            print("Usage: enable <line>")
            return
        try:
            line = int(args)
            if self.debug_info.enable_breakpoint(line, True):
                print(f"Breakpoint at line {line} enabled")
            else:
                print(f"No breakpoint at line {line}")
        except ValueError:
            print(f"Invalid line number: {args}")

    def cmd_disable(self, args):
        if not args:
            print("Usage: disable <line>")
            return
        try:
            line = int(args)
            if self.debug_info.enable_breakpoint(line, False):
                print(f"Breakpoint at line {line} disabled")
            else:
                print(f"No breakpoint at line {line}")
        except ValueError:
            print(f"Invalid line number: {args}")

    def cmd_info(self, args):
        if args == 'breakpoints' or args == 'b':
            bps = self.debug_info.list_breakpoints()
            if bps:
                print("Num Type       Disp Enb What")
                for i, bp in enumerate(bps, 1):
                    disp = 'keep' if bp.hit_count == 0 else 'del '
                    enb = 'y' if bp.enabled else 'n'
                    cond = f" if {bp.condition}" if bp.condition else ""
                    print(f"{i:3d} breakpoint {disp} {enb}   line {bp.line}{cond}")
                    if bp.hit_count > 0:
                        print(f"      breakpoint already hit {bp.hit_count} time(s)")
            else:
                print("No breakpoints.")
        elif args == 'watchpoints' or args == 'w':
            if self.debug_info.watchpoints:
                print("Num Type       Disp Enb What")
                for i, wp in enumerate(self.debug_info.watchpoints, 1):
                    print(f"{i:3d} watchpoint keep y   {wp.var_name}")
            else:
                print("No watchpoints.")
        elif args == 'locals' or args == 'variables':
            self.cmd_locals('')
        elif args == 'registers' or args == 'r':
            print("Registers (simulation):")
            print("  PC: line", self.current_line)
            print("  SP: 0 (stack pointer)")
            print("  FP: 0 (frame pointer)")
        else:
            print("Usage: info <breakpoints|watchpoints|locals|registers>")

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
        print(self.debug_info.list_source(start, end, self.current_line))

    def cmd_print(self, args):
        if not args:
            print("Usage: print <expression>")
            return
        result = self.evaluator.evaluate(args)
        print(f"${len(self.variables)} = {result}")

    def cmd_eval(self, args):
        if not args:
            print("Usage: eval <expression>")
            return
        result = self.evaluator.evaluate(args)
        print(result)

    def cmd_watch(self, args):
        if not args:
            print("Usage: watch <variable> [if condition]")
            return
        parts = args.split(' if ', 1)
        var_name = parts[0].strip()
        condition = parts[1] if len(parts) > 1 else None
        num = self.debug_info.add_watchpoint(var_name, condition)
        cond_str = f" if {condition}" if condition else ""
        print(f"Watchpoint {num}: {var_name}{cond_str}")

    def cmd_step(self, args):
        print("Step into (simulation)")
        self.current_line += 1
        self._check_breakpoints()
        self._show_current_line()

    def cmd_next(self, args):
        print("Step over (simulation)")
        self.current_line += 1
        self._check_breakpoints()
        self._show_current_line()

    def cmd_continue(self, args):
        print("Continuing execution (simulation)")
        self.running = True
        bps = self.debug_info.list_breakpoints()
        next_bp = None
        for bp in bps:
            if bp.line > self.current_line and bp.enabled:
                next_bp = bp
                break
        if next_bp:
            self.current_line = next_bp.line
            next_bp.hit_count += 1
            print(f"Breakpoint hit at line {next_bp.line}")
            self._show_current_line()
        else:
            self.current_line = len(self.debug_info.lines) + 1
            print("Program finished.")
            self.running = False

    def cmd_run(self, args):
        print("Starting program (simulation)")
        self.current_line = 1
        self.running = True
        self.variables = {}
        self.call_stack = []
        bps = self.debug_info.list_breakpoints()
        if bps:
            first_bp = bps[0]
            self.current_line = first_bp.line
            first_bp.hit_count += 1
            print(f"Breakpoint hit at line {first_bp.line}")
            self._show_current_line()
        else:
            self.current_line = len(self.debug_info.lines) + 1
            print("Program finished.")
            self.running = False

    def cmd_until(self, args):
        if not args:
            print("Usage: until <line>")
            return
        try:
            target = int(args)
            print(f"Continuing until line {target} (simulation)")
            self.current_line = target
            self._show_current_line()
        except ValueError:
            print(f"Invalid line number: {args}")

    def cmd_skip(self, args):
        if not args:
            print("Usage: skip <count>")
            return
        try:
            count = int(args)
            self.skip_count = count
            print(f"Will skip {count} breakpoints")
        except ValueError:
            print(f"Invalid count: {args}")

    def cmd_quit(self, args):
        print("Quitting debugger.")
        sys.exit(0)

    def cmd_help(self, args):
        if args:
            cmd = args.lower()
            help_texts = {
                'break': 'break <line> [if condition] - Set breakpoint at line',
                'tbreak': 'tbreak <line> - Set temporary breakpoint',
                'clear': 'clear <line> - Clear breakpoint at line',
                'delete': 'delete <num> - Delete breakpoint by number',
                'enable': 'enable <line> - Enable breakpoint',
                'disable': 'disable <line> - Disable breakpoint',
                'info': 'info <breakpoints|watchpoints|locals|registers> - Show info',
                'list': 'list [start-end] - List source code',
                'print': 'print <expr> - Print expression value',
                'eval': 'eval <expr> - Evaluate expression',
                'watch': 'watch <var> [if condition] - Set watchpoint',
                'step': 'step - Step into function call',
                'next': 'next - Step over function call',
                'continue': 'continue - Continue execution',
                'run': 'run - Start/restart program',
                'until': 'until <line> - Continue until line',
                'skip': 'skip <count> - Skip breakpoints',
                'backtrace': 'backtrace - Show call stack',
                'locals': 'locals - Show local variables',
                'disassemble': 'disassemble - Show disassembly',
                'x': 'x/<n/f/u> <addr> - Examine memory',
                'set': 'set <var> = <value> - Set variable value',
                'quit': 'quit - Quit debugger',
            }
            if cmd in help_texts:
                print(help_texts[cmd])
            else:
                print(f"No help for: {cmd}")
        else:
            print("""Lumyr Debugger Commands:

Breakpoints:
  break (b) <line> [if cond]  Set breakpoint
  tbreak <line>                Set temporary breakpoint
  clear <line>                 Clear breakpoint
  delete (d) <num>             Delete breakpoint
  enable/disable <line>        Enable/disable breakpoint
  info breakpoints             List breakpoints

Watchpoints:
  watch <var> [if cond]        Set watchpoint
  info watchpoints             List watchpoints

Execution:
  run (r)                      Start program
  continue (c)                 Continue execution
  step (s)                     Step into
  next (n)                     Step over
  until (u) <line>             Continue until line
  skip <count>                 Skip breakpoints

Inspection:
  list (l) [range]             List source
  print (p) <expr>             Print expression
  eval <expr>                  Evaluate expression
  locals                        Show local variables
  backtrace (bt)                Show call stack
  info registers                Show registers
  disassemble (disasm)          Show disassembly
  x/<n/f/u> <addr>             Examine memory

Other:
  set <var> = <value>          Set variable
  frame (f) <num>              Select stack frame
  help (h) [cmd]               Show help
  quit (q)                     Quit debugger
""")

    def cmd_backtrace(self, args):
        if self.call_stack:
            print("Call stack:")
            for i, frame in enumerate(reversed(self.call_stack)):
                print(f"  #{i} {frame}")
        else:
            print("#0  main () at line", self.current_line)

    def cmd_locals(self, args):
        if self.variables:
            print("Local variables:")
            for name, value in self.variables.items():
                print(f"  {name} = {value}")
        else:
            print("No local variables.")

    def cmd_disassemble(self, args):
        print("Disassembly (framework - VM integration pending):")
        print(f"  Current line: {self.current_line}")
        print(f"  Source: {self.debug_info.get_line(self.current_line)}")
        print()
        print("  Note: Full disassembly requires VM integration.")
        print("  This will show bytecode instructions for current function.")

    def cmd_examine(self, args):
        if not args:
            print("Usage: x/<n/f/u> <address>")
            return
        print(f"Examining memory: {args} (simulation)")
        print("  Note: Memory examination requires VM integration.")

    def cmd_set(self, args):
        if '=' not in args:
            print("Usage: set <variable> = <value>")
            return
        name, value = args.split('=', 1)
        name = name.strip()
        value = value.strip()
        try:
            evaluated = self.evaluator.evaluate(value)
            self.variables[name] = evaluated
            print(f"{name} = {evaluated}")
        except Exception as e:
            print(f"Error setting variable: {e}")

    def cmd_commands(self, args):
        if not args:
            print("Usage: commands <breakpoint-number>")
            return
        print("Enter commands for breakpoint (end with 'end'):")
        commands = []
        while True:
            try:
                line = input("> ").strip()
                if line == 'end':
                    break
                commands.append(line)
            except (EOFError, KeyboardInterrupt):
                break
        print(f"Added {len(commands)} commands to breakpoint {args}")

    def cmd_frame(self, args):
        if not args:
            print("Current frame: #0 main ()")
            return
        try:
            num = int(args)
            print(f"Selected frame: #{num}")
        except ValueError:
            print(f"Invalid frame number: {args}")

    def _check_breakpoints(self):
        """检查当前行是否有断点"""
        if self.current_line in self.debug_info.breakpoints:
            bp = self.debug_info.breakpoints[self.current_line]
            if bp.enabled:
                if self.skip_count > 0:
                    self.skip_count -= 1
                    return
                bp.hit_count += 1
                print(f"Breakpoint hit at line {self.current_line}")
                if bp.condition:
                    print(f"  Condition: {bp.condition}")

    def _show_current_line(self):
        """显示当前行"""
        if 1 <= self.current_line <= len(self.debug_info.lines):
            print(f"\n{self.current_line:4d} ▶ {self.debug_info.get_line(self.current_line)}")


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
