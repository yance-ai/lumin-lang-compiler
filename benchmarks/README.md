# Lumyr 性能基准测试套件

## 目录结构

```
benchmarks/
├── bench_compute.lm      # 计算密集型（斐波那契、素数、阿克曼函数）
├── bench_string.lm       # 字符串处理（拼接、模板、分割、子串）
├── bench_array.lm        # 数组操作（创建、遍历、排序、二维数组）
├── bench_map.lm          # 映射操作（创建、查找、遍历、删除）
├── bench_gc.lm           # GC 压力测试（大量对象、大数组/映射、对象 churn）
├── bench_math.lm         # 数学运算（算术、浮点、数学函数、位运算）
├── run_benchmarks.py     # 自动运行脚本
└── README.md             # 本文档
```

## 使用方法

### 运行全部基准测试（VM + 编译模式）

```bash
python benchmarks/run_benchmarks.py
```

### 只运行 VM 模式

```bash
python benchmarks/run_benchmarks.py --mode vm
```

### 只运行编译模式

```bash
python benchmarks/run_benchmarks.py --mode compile
```

### 只运行特定基准测试

```bash
python benchmarks/run_benchmarks.py --filter compute
```

### 单独运行某个基准测试

```bash
# VM 模式
./bin/lumyr benchmarks/bench_compute.lm

# 编译模式
./bin/lumyr -c benchmarks/bench_compute.lm -o _bench
./_bench.exe
```

## 输出说明

每个基准测试输出格式：

```
--- bench_compute ---
  VM:      PASS  wall=1.234s  inner=[0.500, 0.300, 0.100]
  Compile: PASS  wall=0.567s  inner=[0.200, 0.150, 0.050]
```

- `wall`: 整个程序的墙钟时间（包括启动、编译等开销）
- `inner`: 程序内部测量的各子测试时间（使用 `clock()` 函数）

## 添加新的基准测试

1. 在 `benchmarks/` 目录下创建 `bench_<name>.lm` 文件
2. 使用 `clock()` 函数测量时间：
   ```lumyr
   t0 = clock()
   // ... 测试代码 ...
   t1 = clock()
   print("test_name time: " + (t1-t0) + "s")
   ```
3. 运行脚本会自动发现并运行新的基准测试

## 注意事项

- 编译模式下，临时可执行文件会自动清理
- 每个基准测试超时时间为 120 秒
- Windows 杀毒软件可能会删除新编译的 .exe 文件，如遇问题请添加排除项
- 建议在空闲系统上运行，避免其他进程影响结果
