// lm_lock.h —— 锁机制：互斥锁 / 递归互斥锁 / 读写锁 / 自旋锁 / 条件变量
// 语义：
//   - mutex() / rmutex() / rwlock() / spinlock() 创建锁，返回锁 id（int，槽位下标）
//   - lock(id) 阻塞加锁（互斥/递归互斥/自旋；读写锁请用 rdlock/wrlock）
//   - unlock(id) 解锁（所有类型通用）
//   - trylock(id) 非阻塞尝试，返回 bool（仅互斥/递归互斥/自旋）
//   - rdlock(id) 读锁（仅读写锁，共享）；wrlock(id) 写锁（仅读写锁，独占）
//   - tryrdlock(id) / trywrlock(id) 读写锁的非阻塞尝试，返回 bool（仅读写锁）
//   - condvar() 创建条件变量，返回条件 id
//   - cond_wait(cond, lock) 释放 lock 并阻塞等待，被唤醒后重新获取 lock 再返回；
//     lock 仅限 mutex/rmutex（自旋/读写锁配条件变量无阻塞释放语义，报错）；
//     调用前必须已持有 lock（pthread 语义，与 C 一致）
//   - cond_signal(cond) 唤醒一个等待者；cond_broadcast(cond) 唤醒全部（无等待者则空操作）
//   - 锁表/条件表动态扩容，无硬上限（受系统资源/OS 限制）；不提供销毁（进程生命周期）
//   - 锁跨线程共享，同一把锁只能由一个线程持有（写锁/互斥），符合 pthread 语义
#ifndef LM_LOCK_H
#define LM_LOCK_H

int lumin_mutex_create(void);    // mutex()：互斥锁
int lumin_rmutex_create(void);   // rmutex()：递归互斥锁（同线程可重复加锁）
int lumin_rwlock_create(void);   // rwlock()：读写锁
int lumin_spinlock_create(void); // spinlock()：自旋锁
void lumin_lock(int id);         // lock(id)：阻塞加锁
void lumin_unlock(int id);       // unlock(id)：解锁
int  lumin_trylock(int id);      // trylock(id)：非阻塞尝试，返回 1/0
void lumin_rdlock(int id);       // rdlock(id)：读锁（共享）
void lumin_wrlock(int id);       // wrlock(id)：写锁（独占）
int  lumin_tryrdlock(int id);    // tryrdlock(id)：读锁非阻塞尝试，返回 1/0（仅读写锁）
int  lumin_trywrlock(int id);    // trywrlock(id)：写锁非阻塞尝试，返回 1/0（仅读写锁）

// ===== 条件变量（与互斥/递归互斥锁配合） =====
int  lumin_condvar_create(void);            // condvar()：创建条件变量
void lumin_cond_wait(int cond, int lock);   // cond_wait(cond, lock)：原子释放 lock 并等待
int  lumin_cond_timedwait(int cond, int lock, long long ms);
                                            // cond_wait_timeout(cond, lock, ms)：限时等待，
                                            // 被唤醒返回 1，超时返回 0（超时后仍持有锁）
void lumin_cond_signal(int cond);           // cond_signal(cond)：唤醒一个等待者
void lumin_cond_broadcast(int cond);        // cond_broadcast(cond)：唤醒全部等待者

#endif // LM_LOCK_H
