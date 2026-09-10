# Lumyr 包管理器

类似 npm/cargo/go mod 的包管理工具，支持依赖管理、版本解析、包发布。

## 功能特性

- **项目初始化**：`lumyr_pkg init` 自动创建项目结构
- **依赖管理**：`add`/`remove`/`install`/`update` 命令
- **语义化版本**：支持 `^`、`~`、`>=`、`<=`、`>`、`<`、`=`、`*` 等版本约束
- **本地注册表**：基于文件系统的包注册表，支持发布和安装
- **Lock 文件**：自动生成 `lumyr-lock.json`，锁定依赖版本
- **脚本运行**：`lumyr_pkg run <script>` 运行项目脚本
- **包搜索**：`lumyr_pkg search` 搜索注册表中的包

## 安装

将 `tools/pkg/lumyr_pkg.py` 复制到 PATH 中，或创建别名：

```bash
alias lumyr_pkg="python /path/to/lumyr-lang-compiler/tools/pkg/lumyr_pkg.py"
```

## 使用方法

### 初始化新项目

```bash
lumyr_pkg init my-project
```

自动创建：
- `lumyr.json`：项目配置文件
- `main.lm`：主程序文件
- `.gitignore`：Git 忽略文件

### 添加依赖

```bash
# 添加最新版本
lumyr_pkg add my-library

# 添加指定版本
lumyr_pkg add my-library 1.2.3

# 添加版本约束
lumyr_pkg add my-library "^1.0.0"
lumyr_pkg add my-library "~1.2.0"
lumyr_pkg add my-library ">=1.0.0 <2.0.0"

# 添加为开发依赖
lumyr_pkg add my-test-framework --dev
```

### 移除依赖

```bash
lumyr_pkg remove my-library
```

### 安装所有依赖

```bash
# 安装所有依赖（包括开发依赖）
lumyr_pkg install

# 只安装生产依赖
lumyr_pkg install --production
```

### 列出依赖

```bash
lumyr_pkg list
```

### 更新依赖

```bash
# 更新所有依赖
lumyr_pkg update

# 更新指定依赖
lumyr_pkg update my-library
```

### 发布包

```bash
# 在项目目录下执行
lumyr_pkg publish
```

包会发布到本地注册表 `~/.lumyr/registry/`。

### 搜索包

```bash
lumyr_pkg search my-lib
```

### 查看包信息

```bash
lumyr_pkg info my-library
```

### 运行脚本

```bash
# 运行 lumyr.json 中定义的脚本
lumyr_pkg run start
lumyr_pkg run build
lumyr_pkg run test
```

## 项目配置文件（lumyr.json）

```json
{
  "name": "my-project",
  "version": "1.0.0",
  "description": "A Lumyr project",
  "main": "main.lm",
  "scripts": {
    "start": "lumyr main.lm",
    "build": "lumyr -c main.lm -o main",
    "test": "lumyr tests/"
  },
  "dependencies": {
    "my-library": "^1.0.0",
    "another-lib": "~2.3.0"
  },
  "devDependencies": {
    "my-test-framework": "^0.5.0"
  }
}
```

## 版本约束语法

| 约束 | 说明 | 示例 | 匹配版本 |
|------|------|------|----------|
| `^` | 兼容更新（不改变最左非零版本号） | `^1.2.3` | `>=1.2.3 <2.0.0` |
| `~` | 保守更新（只改变修订号） | `~1.2.3` | `>=1.2.3 <1.3.0` |
| `>=` | 大于等于 | `>=1.0.0` | `>=1.0.0` |
| `<=` | 小于等于 | `<=2.0.0` | `<=2.0.0` |
| `>` | 大于 | `>1.0.0` | `>1.0.0` |
| `<` | 小于 | `<2.0.0` | `<2.0.0` |
| `=` | 等于 | `=1.2.3` | `=1.2.3` |
| `*` | 任意版本 | `*` | 所有版本 |
| `latest` | 最新版本 | `latest` | 最新版本 |

## 本地注册表

包存储在 `~/.lumyr/registry/` 目录下：

```
~/.lumyr/registry/
├── my-library/
│   ├── 1.0.0/
│   │   ├── package.json
│   │   ├── main.lm
│   │   └── ...
│   ├── 1.1.0/
│   └── 2.0.0/
└── another-lib/
    └── ...
```

## Lock 文件

安装依赖后自动生成 `lumyr-lock.json`：

```json
{
  "name": "my-project",
  "version": "1.0.0",
  "lockfile_version": 1,
  "dependencies": {
    "my-library": {
      "version": "1.2.3",
      "constraint": "^1.0.0",
      "integrity": "sha256-..."
    }
  }
}
```

## 后续计划

1. **远程注册表支持**：HTTP 注册表服务器，支持多人协作
2. **依赖树解析**：递归解析依赖的依赖，处理版本冲突
3. **缓存机制**：下载缓存，避免重复下载
4. **校验和验证**：验证包完整性，防止篡改
5. **Monorepo 支持**：workspaces，多包管理
6. **审计功能**：检查依赖的安全漏洞
7. **许可证检查**：检查依赖的许可证兼容性
8. **与编译器集成**：自动处理模块路径解析

## 参考资料

- [npm package.json 规范](https://docs.npmjs.com/cli/v10/configuring-npm/package-json)
- [Cargo 手册](https://doc.rust-lang.org/cargo/)
- [语义化版本 2.0.0](https://semver.org/lang/zh-CN/)
- [Go Modules](https://go.dev/ref/mod)
