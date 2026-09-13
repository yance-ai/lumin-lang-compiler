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
| 类型 | 说明 | C 对应类型 |
|------|------|-----------|
| `int` | 整数 | `int` |
| `long long` | 长整数 | `long long` |
| `float` | 单精度浮点数 | `float` |
| `double` | 双精度浮点数 | `double` |
| `char` | 字符 | `char` |
| `bool` | 布尔值 | `bool` |
| `string` | 字符串 | `char*` |
| `byte` | 字节 | `unsigned char` |

### 2.4 默认类型
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
```

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

## 12. 解构（unpack）

### 12.1 对象解构
```lumyr
unpack { x, y } = point;
```

### 12.2 数组解构
```lumyr
unpack [a, b, c] = arr;
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
@deprecated
func old_func() {
    // ...
}
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
// 错误
print("hello")

// 正确
print("hello");
```

### 20.2 类型声明用错语法
```lumyr
// 错误（冒号不是类型声明）
func f(p: Point) {}

// 正确（尖括号是类型声明）
func f(<Point> p) {}
```

### 20.3 类型转换用错语法
```lumyr
// 错误（尖括号不是类型转换）
x = <int>3.14;

// 正确（括号是类型转换）
x = (int)3.14;
```

---

*本文档基于 Lumyr 编译器源码和测试文件整理，如有疑问请参考 `src/parse/yacc.y`。*
