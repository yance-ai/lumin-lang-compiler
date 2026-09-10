// lumyr-lang IR 层 peephole 优化 pass
// 第一个优化 pass：常量折叠（constant folding）。
//
// 在 IR 生成完成后、执行/代码生成前，对每个 BytecodeFunc 的指令序列做一遍
// 线性重建式扫描，把运行期才能确定的纯常量运算提前到编译期计算。
//
// 与前端 AST 折叠（ir_compile.c 中 fold_const）的关系：
//   AST 折叠只能覆盖"整棵子树都是字面量"的情形；本 pass 直接在指令流上做
//   peephole，对任何 `LOAD_CONST c1; LOAD_CONST c2; BINOP` /
//   `LOAD_CONST c; UNARY/CAST` 模式一视同仁，是优化管道的中段基础设施，
//   对后续 IR 变换引入的新常量模式自动受益。
//
// 语义安全：折叠直接调用运行时 lumyr_* 函数，结果与执行期逐位一致；
// DIV/MOD 右操作数为 0 时不折叠（保留运行期 inf/NaN 行为）。
#ifndef LUMYR_IR_OPT_H
#define LUMYR_IR_OPT_H

#include "bytecode.h"

// 对单个函数的指令序列做常量折叠优化（原地重建 code 数组，修正跳转目标）。
// 多次折叠可级联（如 1 + 2*3 先折 2*3=6 再折 1+6=7），单遍重建即可完成。
// 返回 1 表示指令流发生了变化（被缩短/改写）。
int ir_opt_constant_fold(BytecodeFunc* fn);

// 优化驱动：对单个 BytecodeFunc 跑常量折叠，迭代至收敛。
// VM / -S / -c 三通道共用此 IR。环境变量 LM_OPT=0 时整体关闭。
// 优化后用 bc_analyze_stack 自校验栈一致性。
// （分支折叠 / DCE / 常量传播 为并行开发中的后续 pass，暂未启用，详见 .c 注释。）
void ir_optimize(BytecodeFunc* fn);

#endif // LUMYR_IR_OPT_H
