# Lumyr WebAssembly 后端

将 Lumyr 源代码编译为 WebAssembly 二进制格式（.wasm），可在浏览器和 Node.js 中运行。

## 当前状态

**基础框架已完成**，支持：
- WASM 二进制格式完整编码器（所有 section）
- 基本整数运算（+、-、*、/、%）
- 整数比较（==、!=、<、>、<=、>=）
- 变量赋值和读取
- print() 输出（通过 JS 导入函数）
- 字符串存储（数据段）
- 生成测试用 HTML 文件

**待实现**：
- 完整的 Lumyr 语法支持（函数定义、控制流、数组、映射等）
- 运行时库移植（GC、字符串操作、标准库）
- 与 VM/编译模式的语义对齐
- 调试信息生成（DWARF for WASM）
- WASI 支持（系统调用）

## 使用方法

### 编译为 WASM

```bash
python tools/wasm/lumyr_wasm.py input.lm output.wasm
```

### 同时生成测试用 HTML

```bash
python tools/wasm/lumyr_wasm.py input.lm output.wasm --html
```

然后在浏览器中打开生成的 HTML 文件即可运行。

### 支持的 Lumyr 语法（当前）

```lumyr
// 整数常量和运算
print(42);
print(1 + 2);
print(10 - 3);
print(5 * 6);
print(20 / 4);
print(17 % 5);

// 变量
x = 10;
y = 20;
print(x + y);
```

## WASM 导入接口

生成的 WASM 模块需要以下 JS 导入：

```javascript
const importObject = {
    env: {
        print_i32: function(x) { console.log(x); },
        print_str: function(ptr, len) {
            const bytes = new Uint8Array(memory.buffer, ptr, len);
            console.log(new TextDecoder().decode(bytes));
        }
    }
};
```

## 导出接口

- `main()`：主函数，程序入口
- `memory`：线性内存（用于字符串和数据存储）

## 架构设计

```
Lumyr 源代码
    ↓
词法分析（复用编译器 lexer）
    ↓
语法分析（复用编译器 parser）
    ↓
AST → WASM IR 翻译
    ↓
WASM 二进制编码
    ↓
.wasm 文件
```

### WASM 二进制结构

1. **Type Section**：函数类型签名
2. **Import Section**：JS 导入函数（print 等）
3. **Function Section**：函数声明
4. **Memory Section**：线性内存
5. **Export Section**：导出接口
6. **Code Section**：函数体指令
7. **Data Section**：字符串等静态数据

## 测试

```bash
# 创建测试文件
echo 'print(42);
print(1 + 2);
x = 10;
y = 20;
print(x + y);' > test_wasm.lm

# 编译为 WASM + HTML
python tools/wasm/lumyr_wasm.py test_wasm.lm test_wasm.wasm --html

# 在浏览器中打开 test_wasm.html
```

## 后续计划

1. **阶段 1**：完整支持基本语法（变量、控制流、函数）
2. **阶段 2**：支持字符串、数组、映射等复合类型
3. **阶段 3**：移植运行时库（GC、标准库）
4. **阶段 4**：性能优化（常量折叠、死代码消除、内联）
5. **阶段 5**：WASI 支持和调试工具

## 参考资料

- [WebAssembly Specification](https://webassembly.github.io/spec/)
- [WASM Binary Format](https://webassembly.github.io/spec/core/binary/index.html)
- [MDN WebAssembly](https://developer.mozilla.org/en-US/docs/WebAssembly)
