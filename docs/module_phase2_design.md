# lumyr 模块系统第二阶段设计文档

## 目标
在第一阶段文本预处理基础上，通过 Name Mangling 实现：
1. 符号隔离：模块私有符号外部不可见
2. 去重导入：同一模块只内联一次
3. 循环导入检测
4. export 语义：仅显式 export 的符号可通过别名 map 访问
5. 双通道一致

## Name Mangling 规则

### 模块 ID
- 每个被导入的模块（非主文件）在首次处理时分配顺序递增的整数 ID
- ID 存储在全局已处理表中，key 为 realpath
- 前缀格式：`__lm_mod_<id>_`，如 `__lm_mod_0_add`

### 需 mangle 的符号（模块内全部顶层声明）
1. `func NAME` → `func __lm_mod_<id>_NAME`
2. 顶层赋值 `NAME = expr` → `__lm_mod_<id>_NAME = expr`
   - 含 `export const NAME = expr`（export 已被剥离为 `NAME = expr`）
   - 解构赋值 `a, b = ...` → 两个标识符均 mangle
3. `type NAME { ... }` → `type __lm_mod_<id>_NAME { ... }`
4. `enum NAME { ... }` → `enum __lm_mod_<id>_NAME { ... }`

### 引用替换
- 模块源码中所有匹配上述符号名的标识符引用均替换为 mangled 名
- 跳过字符串字面量、行注释、块注释、字符字面量内部
- 词边界检查：不替换 `obj.name` 中 `.` 后的部分，不替换更长标识符的子串

### export 符号
- export 的符号同样被 mangle（避免全局命名空间污染）
- 导出 map 映射原名 → mangled 名：`{"add": __lm_mod_0_add, "PI": __lm_mod_0_PI}`
- 外部通过 `alias.add(...)` 访问，等价于 `alias["add"]()` → 拿到 mangled 函数

### 主文件
- 主文件不做 mangle，其符号为程序全局符号
- 主文件中的 import 别名指向模块导出 map

## 去重机制

### 全局已处理表
```c
typedef struct { char* path; int module_id; char* expvar; } ProcessedMod;
```
- key: realpath（绝对路径规范化）
- value: module_id + 导出 map 变量名

### 流程
1. `expand_file()` 入口先查已处理表
2. 命中：直接返回缓存的 expvar，不重复内联模块体
3. 未命中：分配 module_id，处理模块，写入已处理表
4. 调用方仍生成 `alias = expvar;`（同一模块可有多个别名）

## 循环导入检测

### 导入栈
- 递归展开时维护当前正在处理的路径栈（active array）
- 子模块导入前检查目标路径是否已在栈中
- 命中则报错：`[module] 检测到循环导入: <path>`

### 与去重的交互
- 去重检查在循环检测之后：先确认无循环，再确认是否已处理
- A→B→A：处理 A(栈=[A])，A 导入 B(栈=[A,B])，B 导入 A → A 在栈中 → 循环报错 ✓
- A→B, A→C, B→C：处理 B 时 C 入已处理表；A 导入 C 时命中去重，不重复内联 ✓

## 符号发现（transform 扩展）

在现有 `transform()` 单遍扫描中增加花括号深度跟踪：
- 遇到 `{` depth++，`}` depth--（跳过字符串/注释内的花括号）
- 仅在 depth==0 时识别顶层声明：
  - `func` 关键字后第一个标识符 → 记录为模块符号
  - `type` 关键字后第一个标识符 → 记录
  - `enum` 关键字后第一个标识符 → 记录
  - 标识符后紧跟 `=`（非 `==`/`+=`/`-=` 等复合赋值）→ 记录该标识符
  - 解构赋值：标识符后紧跟 `,`，继续收集直到 `=` → 全部记录
- `export func/const` 已在现有逻辑中剥离 export 关键字，符号名正常记录

## 实现结构

### TransformResult 扩展
```c
char** all_symbols;   // 模块内所有顶层符号名
int    nsymbols;
int    cap_symbols;
```

### 新增函数
- `mangle_text(const char* text, char** symbols, int nsym, int mod_id)` → mangled 文本
- `find_processed(const char* realpath)` → ProcessedMod* 或 NULL
- `register_processed(const char* realpath)` → ProcessedMod*（分配 ID）

### 修改函数
- `transform()`：增加 depth 跟踪 + 符号发现 + all_symbols 填充
- `expand_file()`：入口去重检查 → 分配 ID → mangle 文本 → 递归展开子模块 → 生成导出 map（值用 mangled 名）
- `lm_preprocess_main()`：主文件 import 展开时走同一去重逻辑

## 已知限制（Plan A 固有，文档化）
1. 函数内局部变量若与模块级符号同名，会被一并 mangle（语义改变）。
   建议模块内避免局部变量遮蔽模块级符号名。
2. 非真正的符号表隔离，调试时符号名被改写。
3. 后续可升级到方案 B/C（独立 AST 合并 / 可重入 parser）。

## 边界情况处理
- 字符串内含符号名：mangle pass 跳过字符串内部 ✓
- export 函数被模块内其他函数调用：两者均 mangle，调用正确 ✓
- 模块别名与普通变量重名：别名由预处理生成，不在模块符号集中，不 mangle ✓
- 相对路径：基于当前模块所在目录解析，realpath 规范化后去重 ✓
