# lumyr 模块系统（第一阶段）说明

## 目标
在不破坏双通道（VM 解释 / C 编译）一致性、不影响现有 46+ 测试的前提下，落地
`import "path" as name;` / `export func` / `export const` 的最小可用版本。

## 实现方式：文本预处理 + map 导出
lumyr 的 bison/flex parser 是全局不可重入状态，无法在语法动作里递归解析被导入文件。
因此在 `yyparse()` 之前（`src/main.c`）做一次文本预处理（`src/parse/import.c`）：

1. 读主文件全文，单遍扫描（跳过注释 / 字符串 / 字符字面量内部，避免误判其中关键字）。
2. `export func name(...)` → `func name(...)`，记录 `name`；
   `export const name = ...` → 去掉 `export const`，记录 `name`。
3. `import "path" as alias;` → 占位符，随后递归：
   - 相对当前文件目录解析路径（`realpath` 归一化）；
   - 检测循环导入（维护当前递归栈上的绝对路径集合）；
   - 递归内联被导入模块；
   - 在模块末尾生成导出表 `__lmod_exp_N = {"add": add, "PI": PI};`；
   - 把 import 语句替换为 `alias = __lmod_exp_N;`。
4. 合并后的文本写入临时文件，`yyrestart` 交给 parser。

由于两通道消费的是**同一份合并后源码**，VM 与编译通道天然一致。

## 关键语法冲突处理
lumyr 里 `a.b` 本是 map 点访问，但 `a.b(x)` 在 `yacc.y` 中是**方法链糖**
（→ `b(a, x)`，把接收者当前参），并非 `a["b"](x)`。
为此：
- 预处理阶段把所有 `as alias` 登记进模块别名表（`lm_register_alias`）；
- `yacc.y` 方法链规则增加分支：若接收者是已登记的模块别名，
  则 `m.add(1,2)` → `m["add"](1,2)`（map 取值后动态调用，不当前参）。
- 无括号的 `m.PI` 本来就走 map 属性访问，无需改动。

## 支持的能力
- 相对路径导入、子目录导入（`import "nested/strutil.lm" as su;`）。
- 模块嵌套导入（模块里再 import 其他模块）。
- 循环导入检测（报错并中止）。
- `export func`、`export const`。
- 无 import/export 的源文件走原始 fopen 路径，行为与改动前逐字节一致。

## 测试
`tests/module_test/`：
- `math.lm` / `main.lm`：导出 add/sub/PI，验证 `m.add/m.sub/m.PI`、私有符号不在导出表。
- `nested/strutil.lm` + `nested_main.lm`：子目录相对路径导入。
- `cycle_a.lm` / `cycle_b.lm`：循环导入预期报错。

## 已知限制 / 后续计划
1. **符号未真正隔离**：模块里未 export 的函数 / 变量仍会被内联到全局符号表，
   只是不在导出 map 中（`m.internal` 为 null，但 `internal()` 全局仍可调用）。
   真正的隔离需要独立模块符号表 / 命名空间作用域。
2. **重复导入未去重**：同一模块被多次 import 会重复内联，顶层符号可能重名冲突。
   需要模块缓存（按绝对路径去重）。
3. **`import` 仅支持语句形式**：`import ".." as name;` 必须带 `as` 别名；
   暂不支持 `import *` / 具名 `import {add}` / 默认导出。
4. **编译通道仍是内联**：当前两通道都基于同一份内联源码；后续若做独立模块编译单元 /
   增量编译，需要把模块拆成独立编译产物。
5. **标准库拆分**：可基于该机制把 `src/runtime` 里的 math/string/json 等逐步拆成
   可 import 的标准模块。
6. **导出表生成时机**：当前导出 map 在模块代码末尾构建；若被导出符号在前向引用场景下
   有特殊顺序依赖，需要前向声明配合（参考 lambda 前向声明已有的处理）。
