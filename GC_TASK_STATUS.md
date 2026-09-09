# GC tla_global_free 移除任务 — 暂停状态（2026-09-09 中午）

## 已完成
1. ✅ run_benchmarks.sh 路径修复（OPT_DIR 改为相对路径，commit hash 动态获取）
2. ✅ 内存泄漏量化分析：泄漏速率 55-100 MB/s，1000 req/s 下 132 GB/天，4GB 堆 45 分钟 OOM
   - 报告：memory_leak_analysis.md（子代理 artifacts 目录）
3. ✅ 修复 10+ 个 root scanning bug（3 个 commit）：
   - a5ca79c: VAL_FUNC RuntimeFunc 包装、线程返回值保护窗口、STW 轮询竞态、全局根扫描回调、GC_VALID_PTR 防御
   - 8e58543: 编译通道 epilogue 先 protect 再 pop CFrame、编译线程体立即 protect_push、VM 通道退出路径 protect、VM 线程体 protect
   - b31b8d9: 保守式 C 栈扫描兜底
4. ✅ 有界隔离区方案已实施又被用户否决，已 revert（1b5c3fe）

## 当前进行中
- ASan 精准定位 UAF 代理（s_0001KQKgCtK）正在运行：
  - ASan 构建已成功（clang -fsanitize=address -g -O1）
  - 修复了保守栈扫描中 ASan poisoned 红区导致的 SEGV
  - 未提交的改动：src/main.c（ASan 编译标志）、src/runtime/gc_runtime.c（ASan interface）
  - 下一步：将 tla_global_free 改为 free()，运行压力测试，获取 ASan UAF 精确调用栈

## 待完成（下午继续）
1. 获取 ASan UAF 精确调用栈（释放点 + 使用点 + 分配点）
2. 根据调用栈精确定位残留 root scanning 漏洞
3. 修复漏洞
4. 完全移除 tla_global_free（3 处改 free，移除变量定义）
5. 全量回归测试（make test 26/26 + 双通道一致 + 多线程压力测试）
6. 中文 commit + push origin dev

## 关键文件
- 主 GC 文件：src/runtime/gc_runtime.c
- 线程实现：src/runtime/lm_thread.c
- 压力测试：gc_return_race.lm（多线程返回数组，20 线程 × 20 轮）
- 构建：./concat_manifest.sh >/dev/null 2>&1 && make all
- ASan 构建：make CC=clang CFLAGS="-fsanitize=address -g -O1 -Wall -Wextra -I./src -I./generated"
- ASAN_OPTIONS=detect_leaks=0:abort_on_error=1
