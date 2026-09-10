# Lumyr 调试器

源码级调试器，支持断点、单步、变量查看、调用栈查看。

## 当前状态

**基础框架已完成**：
- 命令行调试器界面（类 GDB 风格）
- 源码位置映射和列表显示
- 断点设置/清除/查看
- 单步执行（step/next）框架
- 变量查看框架
- 调用栈查看框架

**待实现**（需要集成到编译器核心）：
- VM 模式调试支持（断点、单步、变量读取）
- 编译模式 DWARF 调试信息生成
- 与 GDB/lldb 的互操作
- 条件断点
- 观察点（watchpoint）
- 表达式求值
- 远程调试（TCP/IP 协议）
- VS Code 调试适配器（DAP）

## 使用方法

```bash
python tools/debugger/lumyr_debug.py your_program.lm
```

## 命令列表

| 命令 | 缩写 | 说明 |
|------|------|------|
| `break <line>` | `b` | 在指定行设置断点 |
| `clear <line>` | - | 清除指定行的断点 |
| `info breakpoints` | `i b` | 列出所有断点 |
| `list [range]` | `l` | 显示源码（默认当前行前后 5 行） |
| `print <var>` | `p` | 打印变量值 |
| `step` | `s` | 单步进入（进入函数调用） |
| `next` | `n` | 单步跳过（不进入函数调用） |
| `continue` | `c` | 继续执行到下一个断点 |
| `run` | `r` | 启动/重启程序 |
| `backtrace` | `bt` | 显示调用栈 |
| `locals` | - | 显示所有局部变量 |
| `quit` | `q` | 退出调试器 |
| `help` | `h` | 显示帮助信息 |

## 使用示例

```
Lumyr Debugger - test.lm
Source: 42 lines
Type 'help' for commands.

(lumyr-dbg) break 10
Breakpoint set at line 10

(lumyr-dbg) info breakpoints
Breakpoints:
  10: x = x + 1;

(lumyr-dbg) list 1-15
   1   func main() {
   2       x = 0;
   3       for (i = 0; i < 10; i = i + 1) {
   4           x = x + i;
   5       }
   6       print(x);
   7   }

(lumyr-dbg) run
Starting program (simulation)
Breakpoint hit at line 10: x = x + 1;

(lumyr-dbg) print x
x = 5

(lumyr-dbg) next
Step over (simulation)
Stopped at line 11: print(x);

(lumyr-dbg) continue
Continuing execution (simulation)
Program finished.

(lumyr-dbg) quit
Quitting debugger.
```

## 架构设计

```
用户命令行界面
    ↓
命令解析与分发
    ↓
调试器核心（断点管理、执行控制、状态查询）
    ↓
调试信息（源码位置映射、变量表、调用栈）
    ↓
VM / 编译后端交互（后续集成）
```

### 调试信息格式

当前使用自定义的轻量级调试信息格式：

```
DebugInfo:
  - source_file: 源文件路径
  - lines: 源码行列表
  - breakpoints: 断点行号集合
  - line_to_instr: 行号 -> 字节码指令映射（待实现）
  - variable_table: 变量名 -> 栈位置映射（待实现）
```

后续将支持 DWARF 标准格式，便于与 GDB/lldb 等标准调试器互操作。

## 后续计划

### 阶段 1：VM 模式调试（近期）
1. 修改 VM 支持断点指令（OPC_BREAKPOINT）
2. 实现单步执行模式（每条指令后检查断点）
3. 实现变量读取（从 VM 栈和局部变量表读取）
4. 实现调用栈查看（遍历 VM 调用栈）
5. 实现条件断点和观察点

### 阶段 2：编译模式调试（中期）
1. 在 C 代码生成时插入源码位置标记（#line 指令）
2. 生成 DWARF 调试信息（.debug_line, .debug_info 等 section）
3. 支持 GDB/lldb 直接调试编译后的可执行文件
4. 变量位置信息（DW_OP 表达式）

### 阶段 3：高级功能（远期）
1. VS Code 调试适配器（实现 Debug Adapter Protocol）
2. 远程调试（TCP/IP 协议，支持嵌入式设备）
3. 时间旅行调试（记录执行历史，支持回退）
4. 表达式求值（在调试器中执行任意 Lumyr 表达式）
5. 多线程调试（线程切换、线程特定断点）

## 参考资料

- [DWARF Debugging Information Format](https://dwarfstd.org/)
- [GDB Documentation](https://www.gnu.org/software/gdb/documentation/)
- [Debug Adapter Protocol](https://microsoft.github.io/debug-adapter-protocol/)
- [WebAssembly DWARF Extensions](https://yurydelendik.github.io/webassembly-dwarf/)
