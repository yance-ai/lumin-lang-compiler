// lm_tls.h —— 线程局部变量（threadlocal）
// 语义：
//   - threadlocal_set(name, value) 写入当前线程的局部槽（深拷贝隔离，跨线程不可见）；
//     返回 value（表达式值）
//   - threadlocal_get(name) 读取当前线程的局部槽；当前线程未初始化过则 runtime_error
//   - 每个线程拥有独立的名字 → 值表（pthread_key 管理，线程退出自动清理）
//   - 名字用字符串；值与语言值语义一致（数组/字符串为深拷贝副本）
#ifndef LM_TLS_H
#define LM_TLS_H

#include "lm_value.h"

void  lumyr_tls_set(const char* name, Value v);   // threadlocal_set(name, value)
Value lumyr_tls_get(const char* name);            // threadlocal_get(name)

#endif // LM_TLS_H
