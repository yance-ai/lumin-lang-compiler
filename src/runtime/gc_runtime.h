#ifndef GC_RUNTIME_H
#define GC_RUNTIME_H

#include <stddef.h>
#include <stdint.h>
#include "ast/lumin_value_type.h"

/* ============================================================
 * 标记-清除 GC（Mark-Sweep）
 *
 * 所有堆分配的 Value 对象前面统一加 GCObject 头：
 *   用户数据指针 = (char*)obj + sizeof(GCObject)
 *   GCObject*    = (GCObject*)((char*)ptr - sizeof(GCObject))
 *
 * vtype 取值为 VAL_STRING/VAL_ARRAY/VAL_MAP/VAL_ERROR/VAL_FUNC，
 * 与 ValueType 枚举一致，用于标记阶段递归遍历内部引用。
 * ============================================================ */

typedef struct GCObject {
    unsigned char marked;
    unsigned char vtype;      /* VAL_STRING / VAL_ARRAY / VAL_MAP / VAL_ERROR / VAL_FUNC */
    uint32_t user_size;       /* 用户数据大小（不含 GCObject 头），利用原 6 字节填充空间，结构体仍为 16 字节 */
    struct GCObject* next;
} GCObject;

/* ---- 分配 / 重分配 ---- */
/* 分配 size + sizeof(GCObject)，初始化头，挂入全局链表，返回用户数据指针 */
void* gc_alloc(size_t size, int vtype);

/* 从链表移除旧节点，realloc，重新挂入，返回新用户数据指针。
 * 只用于 realloc 数组 items 等内部缓冲区（ValueArray* 本身不变，只有 items 指针变） */
void* gc_realloc(void* ptr, size_t new_size);

/* ---- 标记 ---- */
/* 如果 v 是堆类型，标记其 GCObject，然后递归标记内部引用 */
void gc_mark(Value v);

/* 标记原始 GC 指针（用于 buckets/tree/MapEntry 等内部缓冲区，不递归 Value）
 * 返回 1=新标记（需递归子对象），0=已标记/永生/空 */
int gc_mark_ptr(void* ptr);

/* 遍历 VM 栈 stack[0..sp-1]、当前帧及父帧链的局部变量 */
void gc_mark_roots(Value* stack, int sp, StackFrame* frame);

/* ---- 清除 / 回收 ---- */
/* 遍历 g_gc_objects，未标记的释放，清除标记位 */
void gc_sweep(void);

/* mark_roots → sweep → 更新阈值 */
void gc_collect(Value* stack, int sp, StackFrame* frame);

/* ---- 根注册（供 gc_alloc 内部触发 GC 时使用） ---- */
/* VM 执行循环入口调用，注册当前线程的栈/帧；退出时置 NULL */
void gc_set_roots(Value* stack, int* sp_ptr, StackFrame* frame);

/* 获取当前注册的根（用于嵌套 vm_run 保存/恢复） */
void gc_get_roots(Value** stack, int** sp_ptr, StackFrame** frame);

/* ---- GC 暂停/恢复（构造复合对象时防止中间态被 sweep） ---- */
/* 暂停自动 GC（计数器可嵌套）；构造多个 GC 对象期间调用，
   避免第一个对象尚未被根引用时，第二个对象的分配触发 GC 将其回收 */
void gc_disable(void);
/* 恢复自动 GC；计数器归零时若超过阈值则触发一次 GC */
void gc_enable(void);

/* 手动触发一次 GC（使用当前注册的根） */
void gc_collect_now(void);

/* 钉住对象：标记为永生，GC 永不回收（用于常量表中的字符串/数组等） */
void gc_pin(void* ptr);

/* ---- 多线程栈根注册表 ---- */
/* 注册当前线程的操作数栈/栈指针/帧链指针到全局注册表。
 * GC 时遍历所有注册线程，扫描每个线程的 stack[0..*sp] + frame 链局部变量。
 * sp_ptr 和 frame 是指针（动态变化），GC 时解引用获取当前值。
 * 线程退出时必须调用 gc_unregister_thread()。
 * 支持同一线程嵌套注册（栈式语义），unregister 移除最近注册的 entry。 */
void gc_register_thread(Value* stack, int* sp_ptr, StackFrame* frame);

/* 从全局注册表移除当前线程最近注册的 entry（栈式语义） */
void gc_unregister_thread(void);

/* 协作式 STW 安全点：VM 解释循环每条指令前调用，GC 运行时自旋等待 */
void gc_stw_check(void);

/* 统计 */
size_t gc_bytes(void);
size_t gc_count(void);

#endif /* GC_RUNTIME_H */
