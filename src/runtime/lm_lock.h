// lm_lock.h —— 锁机制：互斥锁 / 递归互斥锁 / 读写锁 / 自旋锁
// 语义：
//   - mutex() / rmutex() / rwlock() / spinlock() 创建锁，返回锁 id（int，槽位下标）
//   - lock(id) 阻塞加锁（互斥/递归互斥/自旋；读写锁请用 rdlock/wrlock）
//   - unlock(id) 解锁（所有类型通用）
//   - trylock(id) 非阻塞尝试，返回 bool（仅互斥/递归互斥/自旋）
//   - rdlock(id) 读锁（仅读写锁，共享）；wrlock(id) 写锁（仅读写锁，独占）
//   - 锁表动态扩容，无硬上限（受系统资源/OS 限制）；锁不提供销毁（进程生命周期）
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

#endif // LM_LOCK_H
