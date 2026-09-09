/*
 * lumin 模块系统（第一阶段）——文本预处理层
 *
 * 设计背景：lumin 的 bison/flex parser 是全局不可重入状态，无法在语法动作中
 * 递归解析被导入文件。因此采用「文本预处理」方案：在 yyparse() 之前，把
 * 主文件中的 `import "path" as name;` 语句展开为被导入模块的全部源码内联，
 * 并在模块末尾生成一个导出符号 map（利用 lumin 已有的 `a.b` map 点访问语法，
 * `math.add(1,2)` 天然等价于 `math["add"](1,2)`，无需新增成员访问语法）。
 *
 * 该方案对 VM 通道与编译通道完全一致：两通道消费的是同一份合并后的源码。
 *
 * 已知限制（记录为后续计划）：
 *   - 模块私有符号（未 export）实际仍是全局可见的，只是不在导出 map 中；
 *     真正的符号隔离需要独立符号表。
 *   - 同一模块被多次 import 时，当前会重复内联（顶层符号重名会冲突）。
 */
#ifndef LM_IMPORT_H
#define LM_IMPORT_H

/*
 * 预处理主文件。
 *   src_path：主源文件路径。
 *   had_mod_out：输出标志：
 *       1  -> 确实处理了 import/export，返回值为合并后文本（malloc'd，调用方 free）。
 *       0  -> 源文件不含任何 import/export 语法，返回 NULL（调用方可走原始 fopen 路径）。
 *      -1  -> 出错（错误信息已打印到 stderr），返回 NULL。
 */
char* lm_preprocess_main(const char* src_path, int* had_mod_out);

/*
 * 模块命名空间别名注册表。
 * 预处理阶段把 `import ".." as alias;` 的 alias 登记进来；
 * yacc 在解析 `a.b(x)` 方法链时，若接收者是模块别名，则按 map 动态调用处理
 * （a["b"](x)），而不是把接收者当前参（b(a,x)）。
 */
void lm_register_alias(const char* name);
int  lm_is_module_alias(const char* name);

#endif
