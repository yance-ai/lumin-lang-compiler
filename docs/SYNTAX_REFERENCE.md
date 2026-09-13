# Lumyr 语言语法参考文档

> 本文档记录 Lumyr 语言的所有语法规则，基于 `src/parse/yacc.y` 和测试文件中的实际用法整理。
> **重要：尖括号 `<>` 是类型声明/标注，括号 `()` 是类型转换。**

---

## 1. 基本语法

### 1.1 语句结束
- 所有语句必须以分号 `;` 结束
- 块语句 `{ ... }` 后面不需要分号

```lumyr
print("hello");
x = 42;
```

### 1.2 注释
```lumyr
// 单行注释
/* 多行注释 */
```

### 1.3 变量声明
- 不需要 `let`/`var` 关键字，直接赋值
- 类型标注可选，用尖括号 `<>`

```lumyr
x = 42;              // 默认 long long
y = <int>42;         // 精确类型 int
z = <double>3.14;    // 精确类型 double
s = "hello";         // 默认 string
```

---

## 2. 类型系统

### 2.1 类型声明（尖括号 `<>`）
**尖括号 `<>` 用于类型声明/标注，不是类型转换。**

#### 变量类型标注
```lumyr
a = <int>8;           // 等价于 C: int a = 8;
b = <long long>8;     // 等价于 C: long long b = 8;
c = <double>3.14;     // 等价于 C: double c = 3.14;
d = <float>3.14;      // 等价于 C: float d = 3.14;
e = <char>'a';         // 等价于 C: char e = 'a';
f = <bool>true;        // 等价于 C: bool f = true;
```

#### 函数参数类型标注
```lumyr
func add(<int> a, <int> b) {
    return a + b;
}

func process(<Point> p) {
    // p 是 Point 类型
}
```

#### ref 参数类型标注
```lumyr
func modify(ref <Point> p) {
    p.x = 999;  // 修改会影响外部
}
```

#### 函数返回值类型标注
```lumyr
func add(<int> a, <int> b): <int> {
    return a + b;
}
```

### 2.2 类型转换（括号 `()`）
**括号 `()` 用于类型转换（强转）。**

```lumyr
x = (int)3.14;      // double 转 int
y = (double)42;     // int 转 double
z = (string)42;     // int 转 string
```

### 2.3 支持的基本类型

#### 通用类型
| 类型 | 说明 | C 对应类型 |
|------|------|-----------|
| `int` | 整数 | `int` |
| `short` | 短整数 | `short` |
| `long` | 长整数 | `long` |
| `long long` | 长长整数 | `long long` |
| `uint` | 无符号整数 | `unsigned int` |
| `unsigned short` | 无符号短整数 | `unsigned short` |
| `unsigned long` | 无符号长整数 | `unsigned long` |
| `unsigned char` | 无符号字符 | `unsigned char` |
| `float` | 单精度浮点数 | `float` |
| `double` | 双精度浮点数 | `double` |
| `long double` | 长双精度浮点数 | `long double` |
| `char` | 字符 | `char` |
| `ascii` | ASCII 字符 | `char` |
| `bool` | 布尔值 | `bool` |
| `string` | 字符串 | `char*` |
| `byte` | 字节 | `unsigned char` |
| `void` | 空类型 | `void` |
| `size_t` | 大小类型 | `size_t` |
| `ssize_t` | 有符号大小类型 | `ssize_t` |

#### 固定宽度整数类型
| 类型 | 说明 | C 对应类型 |
|------|------|-----------|
| `int8` | 8 位有符号整数 | `int8_t` |
| `int16` | 16 位有符号整数 | `int16_t` |
| `int32` | 32 位有符号整数 | `int32_t` |
| `int64` | 64 位有符号整数 | `int64_t` |
| `uint8` | 8 位无符号整数 | `uint8_t` |
| `uint16` | 16 位无符号整数 | `uint16_t` |
| `uint32` | 32 位无符号整数 | `uint32_t` |
| `uint64` | 64 位无符号整数 | `uint64_t` |

#### FFI 特殊类型
| 类型 | 说明 |
|------|------|
| `ptr` | 通用指针 |
| `pointer` | 指针（同 ptr） |
| `handle` | 句柄（同 ptr） |

### 2.4 类型标注的使用场景

类型标注（尖括号 `<>`）可以用于以下场景：

#### 变量声明
```lumyr
a = <int>8;           // 等价于 C: int a = 8;
b = <double>3.14;     // 等价于 C: double b = 3.14;
c = <int8>1000;       // 截断为 int8: -24
d = <uint8>(-1);      // 转换为 uint8: 255
```

#### 非常量表达式
```lumyr
x = 1000;
print(<int8>x);        // -24
print(<uint16>(x * 2)); // 1000
```

#### 复合表达式
```lumyr
print(<int8>(100 + 900));  // -24 (先计算再截断)
print(<int8>100 + 900);    // 1000 (只截断 100，再加 900)
```

#### 函数返回值
```lumyr
func get_val() { return 1000; }
print(<int8>get_val());  // -24
```

### 2.5 数组泛型类型标注

```lumyr
// 基本类型数组
ai = <int>[1, 2, 3];
ad = <double>[1.1, 2.2];
ab = <bool>[true, false, 1];
ac = <char>['a', 'b', 99];
as_ = <string>["x", "y", 123];
aby = <byte>[200, 256, -1];

// 固定宽度整数数组
ai8 = <int8>[200, 127, 128];
au8 = <uint8>[200, 256, -1];
ai32 = <int32>[2147483648, 100];

// 空数组
e1 = <int>[];
e2 = <string>[];
e3 = <uint8>[];
e4 = <long long>[];
```

### 2.6 Map 泛型类型标注

```lumyr
// 基本值类型 Map
mi = <string,int>{"a": 1, "b": 2};
md = <string,double>{"x": 1.5};
mb = <string,bool>{"t": true, "f": 0};
mc = <string,char>{"k": 'A', "n": 66};
ms = <string,string>{"a": "1", "b": 2};
mby = <string,byte>{"a": 200, "b": 256};

// 固定宽度整数 Map
mi8 = <string,int8>{"a": 200, "b": 127};
mu8 = <string,uint8>{"a": 200, "b": -1};
mi32 = <string,int32>{"big": 2147483648};
```

### 2.7 默认类型
- 整数默认 `long long`
- 浮点数默认 `double`
- 没有类型标注时，Value 默认给上 `long long` 的类型标记

---

## 3. 函数定义

### 3.1 基本函数定义
```lumyr
func name(params) {
    // 函数体
}
```

### 3.2 带返回值类型标注
```lumyr
func add(<int> a, <int> b): <int> {
    return a + b;
}
```

### 3.3 默认参数
```lumyr
func greet(name, greeting = "Hello") {
    print(greeting, name);
}
```

### 3.4 可变参数
```lumyr
func sum(...args) {
    total = 0;
    for x in args {
        total = total + x;
    }
    return total;
}
```

### 3.5 ref 引用传递参数
```lumyr
func modify(ref <Point> p) {
    p.x = 999;  // 修改会影响外部
}
```

### 3.6 生成器函数
```lumyr
gen func countdown(n) {
    while (n > 0) {
        yield n;
        n = n - 1;
    }
}
```

### 3.7 编译期常量函数
```lumyr
const func square(x) {
    return x * x;
}
```

### 3.8 泛型函数
```lumyr
func<T> identity(x: T) {
    return x;
}
```

### 3.9 运算符重载
```lumyr
func +(other) {
    return Point(x + other.x, y + other.y);
}
```

### 3.10 FFI 外部函数声明
```lumyr
extern <int> func printf(<string> format, ...);
extern "libcurl" <int> func curl_easy_init();
extern "msvcrt.dll" <ptr> func strchr(<string>s, <int>c);
extern "msvcrt.dll" <void> func srand(<int>seed);
extern "msvcrt.dll" <handle> func malloc(<int>size);
```

### 3.11 Lambda 匿名函数

Lambda 是匿名函数，可以直接赋值给变量或作为参数传递。

```lumyr
// 基本 lambda
g = func(x) { return x * 2 + 5; };
print(g(5));  // 15

// 多参数 lambda
add_fn = func(a, b) { return a + b; };
print(add_fn(3, 4));  // 7

// 作为高阶参数传递
arr = [1, 2, 3, 4, 5];
print(join(map(arr, func(x) { return x * x; }), ","));   // 1,4,9,16,25
print(join(filter(arr, func(x) { return x % 2 == 0; }), ","));  // 2,4
print(reduce(arr, func(acc, v) { return acc + v; }, 0));  // 15

// lambda 访问全局变量
g_multiplier = 3;
mul_global = func(x) { return x * g_multiplier; };
print(mul_global(7));  // 21
```

**注意**：Lumyr 的 lambda 是"无闭包"设计——编译期强制禁止 lambda 体访问外层函数局部变量，只能访问：自身参数、全局变量、以及 lambda 体内赋值产生的局部变量。这从根本上杜绝了 UAF 和悬垂引用。

---

## 4. 结构体（struct）

### 4.1 struct 定义
```lumyr
struct Point {
    x: int,
    y: int,
    func distance(other: Point): <double> {
        return (x - other.x)^2 + (y - other.y)^2;
    }
}
```

### 4.2 struct 构造
```lumyr
p = Point(10, 20);
```

### 4.3 struct 字段访问
```lumyr
print(p.x, p.y);
p.x = 100;
```

### 4.4 struct 方法调用
```lumyr
d = p.distance(other);
```

### 4.5 struct 嵌套
```lumyr
struct Rect {
    top_left: Point,
    bottom_right: Point,
}

r = Rect(Point(0, 0), Point(100, 100));
print(r.top_left.x);
```

### 4.6 struct 只读属性
- `__structname__` — struct 类型名（只读）
- `__mapname__` — map 类型名（只读，原 `__classname__` 改名）

---

## 5. 类型别名（type）

### 5.1 type 定义
```lumyr
type Person {
    name: string,
    age: int,
}
```

### 5.2 type 实现接口
```lumyr
type Dog implements Printable {
    name: string,
    func to_string(): <string> {
        return "Dog: " + name;
    }
}
```

### 5.3 泛型 type
```lumyr
type<T> Box {
    value: T,
}
```

---

## 6. 接口（interface）

### 6.1 interface 定义
```lumyr
interface Printable {
    func to_string(): <string>;
}
```

### 6.2 interface 继承
```lumyr
interface Colored extends Printable {
    func get_color(): <string>;
}
```

---

## 7. 枚举（enum）

```lumyr
enum Color {
    RED,
    GREEN,
    BLUE,
}
```

---

## 8. 控制流

### 8.1 if/else
```lumyr
if (x > 0) {
    print("positive");
} else if (x < 0) {
    print("negative");
} else {
    print("zero");
}
```

### 8.2 while 循环
```lumyr
while (i < 10) {
    print(i);
    i = i + 1;
}
```

### 8.3 do-while 循环
```lumyr
do {
    print(i);
    i = i + 1;
} while (i < 10);
```

### 8.4 C 风格 for 循环
```lumyr
for (i = 0; i < 10; i = i + 1) {
    print(i);
}
```

### 8.5 for-each 循环（数组）
```lumyr
for x in arr {
    print(x);
}
```

### 8.6 for-each 循环（map，键值对）
```lumyr
for k, v in mymap {
    print(k, v);
}
```

### 8.7 迭代器协议
```lumyr
for x iter obj {
    print(x);
}
```

### 8.8 switch
```lumyr
switch (x) {
    case 1:
        print("one");
        break;
    case 2:
        print("two");
        break;
    default:
        print("other");
}
```

### 8.8.1 match（switch 别名）

`match` 是 `switch` 的别名，语法完全相同，支持字面量匹配和 default 通配符。

```lumyr
// 整数匹配
x = 2;
match (x) {
    case 1:
        print("one");
        break;
    case 2:
        print("two");
        break;
    default:
        print("other");
}

// 字符串匹配
s = "hello";
match (s) {
    case "hello":
        print("greeting");
        break;
    case "bye":
        print("farewell");
        break;
    default:
        print("unknown");
}

// 无 break 的 fallthrough（C 风格）
n = 1;
match (n) {
    case 1:
        print("case1");
    case 2:
        print("case2");
        break;
    default:
        print("default");
}
```

### 8.9 break / continue
```lumyr
break;     // 跳出循环
continue;  // 跳过本次循环
```

### 8.10 return
```lumyr
return value;
return;  // 无返回值
```

---

## 9. 异常处理

### 9.1 try/catch/finally
```lumyr
try {
    // 可能出错的代码
} catch (e) {
    print("Error:", e);
} finally {
    print("always executed");
}
```

### 9.2 throw
```lumyr
throw "error message";
throw Error("something went wrong");
```

---

## 10. 生成器（generator）

### 10.1 生成器定义
```lumyr
gen func range(start, end) {
    i = start;
    while (i < end) {
        yield i;
        i = i + 1;
    }
}
```

### 10.2 生成器使用
```lumyr
g = range(0, 5);
print(next(g));  // 0
print(next(g));  // 1
```

### 10.3 生成器方法
- `.next()` — 获取下一个值
- `.throw(err)` — 向生成器抛出异常
- `.close()` — 关闭生成器

### 10.4 生成器组合操作
- `chain(g1, g2)` — 连接两个生成器
- `zip(g1, g2)` — 合并两个生成器
- `map(g, fn)` — 映射生成器
- `filter(g, fn)` — 过滤生成器

---

## 11. 宏（macro）

### 11.1 宏定义
```lumyr
macro DEBUG(msg) {
    print("[DEBUG]", msg);
}
```

### 11.2 宏调用
```lumyr
DEBUG("hello");  // 编译期展开
```

---

## 12. 解构与展开

### 12.1 解构赋值（不需要 unpack 关键字）

直接用逗号分隔的变量列表从数组或函数返回值中解构。

```lumyr
// 数组解构
a, b = [1, 2];
print(a);  // 1
print(b);  // 2

// 多类型解构
x, y, z = ["hello", 3.14, true];

// 函数返回值解构
func mkarr() { return [10, 20, 30]; }
p, q, r = mkarr();
```

### 12.2 unpack 关键字解构

```lumyr
// 对象解构
unpack { x, y } = point;

// 数组解构
unpack [a, b, c] = arr;
```

### 12.3 展开运算符（...）

#### 数组展开
```lumyr
arr = [3, 4];
r1 = [1, 2, ...arr, 5];      // [1, 2, 3, 4, 5]
r2 = [...arr, ...arr];        // [3, 4, 3, 4]
r3 = [...[100, 200], 300];   // [100, 200, 300]
```

#### Map 展开
```lumyr
m1 = {"a": 1, "b": 2};
m2 = {...m1, "c": 3};         // {"a": 1, "b": 2, "c": 3}

// 后面的键覆盖前面的
m3 = {"x": 10};
m4 = {...m3, "x": 20, "y": 30};  // {"x": 20, "y": 30}
```

#### 解构 + 展开组合
```lumyr
data = [1, [2, 3], 4];
first, inner, last = data;
combined = [...inner, first, last];  // [2, 3, 1, 4]
```

---

## 13. 扩展方法（extend）

```lumyr
extend String {
    func shout(): <string> {
        return this + "!!!";
    }
}
```

---

## 14. 注解（annotation）

```lumyr
// 无参数注解
@deprecated
func old_func() {
    // ...
}

// 带参数注解
@deprecated("use new_func instead")
func old_func2() {
    // ...
}

// 其他常用注解
@log
@test
@cached
@optimized
@performance_critical
```

---

## 15. 数据结构

### 15.1 数组
```lumyr
arr = [1, 2, 3, 4, 5];
print(arr[0]);      // 1
arr[0] = 100;
print(len(arr));    // 5
```

### 15.2 Map（字典）
```lumyr
m = {"a": 1, "b": 2};
print(m["a"]);      // 1
m["c"] = 3;
print(keys(m));     // ["a", "b", "c"]
```

### 15.3 字符串
```lumyr
s = "hello";
print(s[0]);        // 'h'
print(len(s));      // 5
print(s + " world"); // "hello world"
```

### 15.4 字符串插值
```lumyr
name = "World";
print(f"Hello, {name}!");  // "Hello, World!"
```

---

## 16. 运算符

### 16.1 算术运算符
| 运算符 | 说明 |
|--------|------|
| `+` | 加法 |
| `-` | 减法 |
| `*` | 乘法 |
| `/` | 除法 |
| `%` | 取模 |
| `^` | 幂运算 |

### 16.2 比较运算符
| 运算符 | 说明 |
|--------|------|
| `==` | 等于 |
| `!=` | 不等于 |
| `<` | 小于 |
| `>` | 大于 |
| `<=` | 小于等于 |
| `>=` | 大于等于 |

### 16.3 逻辑运算符
| 运算符 | 说明 |
|--------|------|
| `&&` | 逻辑与 |
| `||` | 逻辑或 |
| `!` | 逻辑非 |

### 16.4 位运算符
| 运算符 | 说明 |
|--------|------|
| `&` | 按位与 |
| `|` | 按位或 |
| `~` | 按位取反 |
| `<<` | 左移 |
| `>>` | 右移 |

### 16.5 赋值运算符
| 运算符 | 说明 |
|--------|------|
| `=` | 赋值 |
| `+=` | 加并赋值 |
| `-=` | 减并赋值 |
| `*=` | 乘并赋值 |
| `/=` | 除并赋值 |
| `%=` | 取模并赋值 |
| `++` | 自增（前缀/后缀） |
| `--` | 自减（前缀/后缀） |

### 16.6 空安全运算符
| 运算符 | 说明 |
|--------|------|
| `?.` | 安全调用 |
| `??` | 空值合并 |

---

## 17. 模块系统

### 17.1 import
```lumyr
import "module.lm";
```

### 17.2 export
```lumyr
export func my_func() {
    // ...
}
```

---

## 18. 内置函数

### 18.1 常用函数
- `print(...)` — 打印
- `len(x)` — 长度
- `type(x)` — 类型
- `keys(m)` — map 的键列表
- `next(g)` — 生成器下一个值
- `string(x)` — 转字符串
- `int(x)` — 转整数
- `double(x)` — 转浮点数

### 18.2 生成器函数
- `GenThrow(gen, err)` — 向生成器抛出异常
- `chain(g1, g2)` — 连接生成器
- `zip(g1, g2)` — 合并生成器
- `map(g, fn)` — 映射生成器
- `filter(g, fn)` — 过滤生成器

### 18.3 线程与并发

#### 线程创建
```lumyr
// 创建线程，执行函数并传递参数
t = thread(worker_func, arg);
t2 = thread(worker_func2);  // 无参数

// 等待线程结束
join(t);
```

#### 互斥锁（Mutex）
```lumyr
ml = mutex();          // 创建互斥锁
lock(ml);              // 加锁
// 临界区代码
unlock(ml);            // 解锁
```

#### 条件变量（Condition）
```lumyr
cond = cond();         // 创建条件变量
wait(cond, mutex);     // 等待条件
signal(cond);          // 唤醒一个等待线程
broadcast(cond);       // 唤醒所有等待线程
```

### 18.4 数组与 Map 方法

#### 数组方法
```lumyr
arr = [1, 2, 3];

// 添加与删除
arr = arr.add(4);              // 添加元素 → [1,2,3,4]
arr = arr.addAll([5, 6]);      // 批量添加
arr = arr.remove(1);            // 删除下标 1 的元素
arr = arr.del(1);               // 同 remove
arr = arr.insert(1, 99);        // 在下标 1 插入 99
arr = arr.clear();              // 清空数组

// 变换与查询
sorted = arr.sort();             // 排序
reversed = arr.reverse();        // 反转
sum_val = arr.sum();             // 求和
avg_val = arr.avg();             // 求平均值
has_2 = arr.contains(2);         // 是否包含

// 方法链
x = [].add(1).add(2).add(3).add(4);
print(join([3, 1, 2].sort(), ","));  // "1,2,3"
```

**注意**：数组方法是引用语义，`add` 会原地修改原数组。

#### Map 方法
```lumyr
m = {"a": 1, "b": 2};

// 方法调用
m.set("c", 3);                  // 设置键值
keys = m.keys();                 // 获取键列表
values = m.values();             // 获取值列表
```

#### Map 点属性访问

Map 支持点语法访问和赋值属性（等价于下标访问）。

```lumyr
m = {"a": 1, "b": "x", "c": [1, 2]};

// 点属性读
print(m.a);                      // 1（等价 m["a"]）
print(m.b);                      // "x"
print(m.c[1]);                   // 2（属性取到数组再下标）

// 点属性赋值
m.d = 42;                        // 新增键
m.a = 100;                       // 覆盖已有键
m.e = [7, 8];

// 嵌套组合
cfg = {"data": [1, 2, 3], "opts": {"flag": true}};
cfg.data = cfg.data.add(4);     // 属性读+数组方法+属性赋值
print(cfg.data.remove(0)[0]);   // 2
```

---

## 19. 重要语法规则总结

### 19.1 类型声明 vs 类型转换
| 语法 | 用途 | 示例 |
|------|------|------|
| `<>` | 类型声明/标注 | `a = <int>8;` `func f(<int> x)` |
| `()` | 类型转换（强转） | `a = (int)3.14;` |

### 19.2 参数类型
| 语法 | 说明 |
|------|------|
| `p` | 普通参数（值传递） |
| `<int> p` | 带类型标注的普通参数 |
| `ref p` | 引用传递参数 |
| `ref <int> p` | 带类型标注的引用传递参数 |
| `p = 10` | 带默认值的参数 |
| `...args` | 可变参数 |

### 19.3 函数类型
| 语法 | 说明 |
|------|------|
| `func name() {}` | 普通函数 |
| `gen func name() {}` | 生成器函数 |
| `const func name() {}` | 编译期常量函数 |
| `func<T> name() {}` | 泛型函数 |
| `extern func name();` | FFI 外部函数声明 |

### 19.4 循环类型
| 语法 | 说明 |
|------|------|
| `while (cond) {}` | while 循环 |
| `do {} while (cond);` | do-while 循环 |
| `for (init; cond; incr) {}` | C 风格 for 循环 |
| `for x in arr {}` | for-each 数组循环 |
| `for k, v in map {}` | for-each map 循环 |
| `for x iter obj {}` | 迭代器协议循环 |

---

## 20. 常见错误

### 20.1 忘记分号
```lumyr
// 错误（会报"语法错误(第2行)"）
print("hello")

// 正确
print("hello");
```

### 20.2 类型声明用错语法（冒号 vs 尖括号）
```lumyr
// 错误（冒号不是类型声明，会报语法错误）
func f(p: Point) {}
func modify(ref p: Point) {}

// 正确（尖括号是类型声明）
func f(<Point> p) {}
func modify(ref <Point> p) {}
```

### 20.3 类型转换用错语法
```lumyr
// 错误（尖括号不是类型转换，是类型声明）
x = <int>3.14;

// 正确（括号是类型转换）
x = (int)3.14;
```

### 20.4 ref 参数缺少类型标注时的语法
```lumyr
// 正确（ref 参数可以不带类型标注）
func modify(ref p) {}

// 正确（ref 参数带类型标注）
func modify(ref <Point> p) {}
```

### 20.5 复合表达式类型标注的优先级
```lumyr
// <int8>(100 + 900)：先计算再截断，结果 -24
print(<int8>(100 + 900));

// <int8>100 + 900：只截断 100，再加 900，结果 1000
print(<int8>100 + 900);
```

### 20.6 lambda 闭包限制
```lumyr
// 错误：lambda 不能访问外层函数局部变量（编译期禁止）
func outer() {
    x = 10;
    f = func() { return x; };  // 错误：x 是外层局部变量
}

// 正确：lambda 只能访问全局变量和自身参数
g_x = 10;  // 全局变量
f = func() { return g_x; };  // 正确
```

### 20.7 函数内赋值总是局部（词法遮蔽）
```lumyr
counter = 0;  // 全局变量
bump = func() {
    counter = counter + 1;  // 这里的 counter 是局部变量，不是全局
    return counter;
};
print(bump());   // 1（读全局 counter=0，+1=1，但写入为局部）
print(bump());   // 1（每次调用都读全局 counter=0）
print(counter);  // 0（全局 counter 从未被修改）
```

---

*本文档基于 Lumyr 编译器源码和 90+ 测试文件整理，如有疑问请参考 `src/parse/yacc.y` 和 `tests/` 目录。*
