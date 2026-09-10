# Lumyr LSP (Language Server Protocol)

## 功能特性
- 文档同步（didOpen/didChange/didSave/didClose）
- 悬停提示（hover）— 显示函数签名、类型、文档
- 跳转定义（definition）— 跳转到函数/变量/类型定义处
- 自动补全（completion）— 关键字、内置函数、当前文件符号
- 诊断（diagnostics）— 词法错误、语法错误（通过编译器）

## 项目结构
```
tools/lsp/
├── lumyr_lsp/              # Python LSP 服务器
│   ├── __init__.py
│   ├── __main__.py         # 入口: python -m lumyr_lsp
│   ├── protocol.py          # LSP 协议类型
│   ├── server.py            # LSP 服务器核心 (JSON-RPC over stdio)
│   ├── documents.py         # 文档管理
│   ├── analysis.py         # 词法分析、符号提取、诊断
│   └── features.py          # hover/definition/completion 功能
├── vscode-extension/        # VS Code 客户端扩展
│   ├── package.json
│   ├── extension.js
│   ├── language-configuration.json
│   └── syntaxes/
│       └── lumyr.tmLanguage.json
├── README.md
└── pyproject.toml
```

## 环境要求
- Python 3.8+
- VS Code 1.75+
- （可选）Lumyr 编译器已构建，用于更精确的语法诊断

## 安装与使用

### 方法一：在 VS Code 中直接加载扩展（开发模式）
1. 打开 VS Code
2. 选择 "文件" → "打开文件夹"，打开 `tools/lsp/vscode-extension` 目录
3. 按 F5 启动扩展开发宿主（Extension Development Host）
4. 在新窗口中打开任意 .lm 文件，LSP 自动激活

### 方法二：打包为 VSIX 安装
```bash
cd tools/lsp/vscode-extension
# 需要 npm install -g @vscode/vsce
vsce package
# 生成 lumyr-lsp-0.1.0.vsix
# 在 VS Code 中: 扩展 → ... → 从 VSIX 安装
```

### 方法三：手动配置（其他编辑器）
LSP 服务器通过 stdio 通信，启动命令：
```bash
cd tools/lsp
python -m lumyr_lsp
```
在支持 LSP 的编辑器（Neovim、Emacs、Sublime Text 等）中配置此命令即可。

## 配置项
在 VS Code settings.json 中：
```json
{
  "lumyr-lsp.pythonPath": "python",
  "lumyr-lsp.serverPath": ""
}
```
- `lumyr-lsp.pythonPath`: Python 可执行文件路径
- `lumyr-lsp.serverPath`: lumyr_lsp 包所在目录，留空自动检测

## 故障排查
- **LSP 不启动**：检查 Python 是否在 PATH 中，尝试设置 `lumyr-lsp.pythonPath` 为完整路径
- **无诊断信息**：确保 Lumyr 编译器已构建（在项目根目录运行 build.bat build），LSP 会自动检测编译器路径
- **中文乱码**：确保系统区域设置为 UTF-8，或在 VS Code 设置中配置文件编码
- **Windows 路径**：LSP 内部使用 file:/// URI，Windows 路径会自动转换

## 开发说明
- LSP 服务器不依赖任何第三方 Python 包，纯标准库实现
- 词法分析器移植自 src/lex/lex.l
- 诊断优先使用编译器输出，降级到内置词法分析
- 不要修改 src/ 下的编译器核心代码

## License
Apache License 2.0（与主项目一致）
