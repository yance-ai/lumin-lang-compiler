// Lumyr Language Server Client (纯 JavaScript，无第三方 npm 依赖)
// 使用 VS Code 内置 API + Node.js 内置模块手动实现 JSON-RPC over stdio

const vscode = require('vscode');
const { spawn } = require('child_process');
const path = require('path');

class LumyrLanguageClient {
    constructor() {
        this.process = null;
        this.requestId = 0;
        this.pendingRequests = new Map();
        this.buffer = Buffer.alloc(0);
        this.diagnostics = null;
        this.initialized = false;
        this.initializeResolve = null;
        this.initializedPromise = null;
    }

    /**
     * 启动 LSP 服务器子进程并完成 initialize 握手
     */
    async start() {
        const config = vscode.workspace.getConfiguration('lumyr-lsp');
        const pythonPath = config.get('pythonPath', 'python');
        const configuredServerPath = config.get('serverPath', '');

        // 确定 lumyr_lsp 包所在目录（tools/lsp/）
        const packageDir = this._resolvePackageDir(configuredServerPath);
        if (!packageDir) {
            throw new Error(
                '找不到 lumyr_lsp 包目录。请在设置中配置 lumyr-lsp.serverPath。'
            );
        }

        const cwd = packageDir;
        const args = ['-m', 'lumyr_lsp'];

        console.log(`[Lumyr LSP] 启动服务器: ${pythonPath} ${args.join(' ')} (cwd=${cwd})`);

        // Windows 上确保子进程以 UTF-8 输出，避免中文乱码
        const env = Object.assign({}, process.env, { PYTHONIOENCODING: 'utf-8' });

        this.process = spawn(pythonPath, args, {
            cwd: cwd,
            stdio: ['pipe', 'pipe', 'pipe'],
            env: env
        });

        this.process.stdout.setEncoding('binary');
        this.process.stdin.setDefaultEncoding('binary');

        this.process.stdout.on('data', (chunk) => {
            this.buffer = Buffer.concat([this.buffer, Buffer.from(chunk, 'binary')]);
            this._parseBuffer();
        });

        this.process.stderr.on('data', (chunk) => {
            const text = chunk.toString('utf8');
            console.error(`[Lumyr LSP] stderr: ${text}`);
        });

        this.process.on('exit', (code, signal) => {
            console.warn(`[Lumyr LSP] 服务器进程退出: code=${code}, signal=${signal}`);
            this.initialized = false;
            // 拒绝所有未决请求
            for (const [, resolver] of this.pendingRequests) {
                if (resolver.reject) {
                    resolver.reject(new Error('LSP 服务器进程已退出'));
                }
            }
            this.pendingRequests.clear();
            if (code !== 0 && code !== null) {
                vscode.window.showErrorMessage(
                    `Lumyr LSP 服务器异常退出 (code=${code})。请检查 Python 路径和 lumyr_lsp 包是否正确。`
                );
            }
        });

        this.process.on('error', (err) => {
            console.error(`[Lumyr LSP] 进程错误: ${err.message}`);
            vscode.window.showErrorMessage(
                `无法启动 Lumyr LSP 服务器: ${err.message}。请检查 lumyr-lsp.pythonPath 设置。`
            );
        });

        this.diagnostics = vscode.languages.createDiagnosticCollection('lumyr');

        // 发送 initialize 请求
        const initParams = {
            processId: process.pid,
            rootUri: vscode.workspace.workspaceFolders && vscode.workspace.workspaceFolders.length > 0
                ? vscode.workspace.workspaceFolders[0].uri.toString()
                : null,
            capabilities: {
                textDocument: {
                    synchronization: {
                        didOpen: true,
                        didChange: true,
                        didSave: true,
                        didClose: true,
                        dynamicRegistration: false
                    },
                    hover: {
                        dynamicRegistration: false,
                        contentFormat: ['plaintext', 'markdown']
                    },
                    definition: { dynamicRegistration: false },
                    completion: {
                        dynamicRegistration: false,
                        completionItem: { snippetSupport: false }
                    }
                },
                workspace: {}
            }
        };

        await this.sendRequest('initialize', initParams);
        this.initialized = true;
        this.sendNotification('initialized', {});

        // 重新打开当前已有的文档
        for (const doc of vscode.workspace.textDocuments) {
            if (doc.languageId === 'lumyr' || doc.fileName.endsWith('.lm')) {
                this._didOpen(doc);
            }
        }
    }

    /**
     * 确定 lumyr_lsp 包目录
     */
    _resolvePackageDir(configuredServerPath) {
        // 1. 用户显式配置
        if (configuredServerPath && configuredServerPath.trim() !== '') {
            const candidate = path.resolve(configuredServerPath);
            if (this._isValidPackageDir(candidate)) {
                return candidate;
            }
        }

        // 2. 扩展目录的上一级（extension.js 在 vscode-extension/ 下，包在 tools/lsp/）
        const relative = path.join(__dirname, '..');
        if (this._isValidPackageDir(relative)) {
            return relative;
        }

        // 3. 在 workspace folders 中查找 tools/lsp
        if (vscode.workspace.workspaceFolders) {
            for (const folder of vscode.workspace.workspaceFolders) {
                const p = path.join(folder.uri.fsPath, 'tools', 'lsp');
                if (this._isValidPackageDir(p)) {
                    return p;
                }
            }
        }

        return null;
    }

    _isValidPackageDir(dir) {
        try {
            const pkgInit = path.join(dir, 'lumyr_lsp', '__main__.py');
            return require('fs').existsSync(pkgInit);
        } catch (e) {
            return false;
        }
    }

    /**
     * 发送请求，返回 Promise
     */
    sendRequest(method, params) {
        const id = ++this.requestId;
        const msg = { jsonrpc: '2.0', id: id, method: method, params: params };
        this._write(msg);

        return new Promise((resolve, reject) => {
            this.pendingRequests.set(id, { resolve, reject, method: method });
            // 超时保护（30 秒）
            setTimeout(() => {
                if (this.pendingRequests.has(id)) {
                    this.pendingRequests.delete(id);
                    reject(new Error(`LSP 请求超时: ${method}`));
                }
            }, 30000);
        });
    }

    /**
     * 发送通知（无 id）
     */
    sendNotification(method, params) {
        const msg = { jsonrpc: '2.0', method: method, params: params };
        this._write(msg);
    }

    _write(msg) {
        if (!this.process || !this.process.stdin || this.process.stdin.destroyed) {
            console.warn(`[Lumyr LSP] stdin 已关闭，无法发送: ${msg.method}`);
            return;
        }
        const body = Buffer.from(JSON.stringify(msg), 'utf8');
        const header = Buffer.from(`Content-Length: ${body.length}\r\n\r\n`, 'ascii');
        this.process.stdin.write(header);
        this.process.stdin.write(body);
    }

    /**
     * 从 stdout buffer 中解析完整的 LSP 消息
     */
    _parseBuffer() {
        // 循环解析，直到 buffer 不足一个完整消息
        for (;;) {
            // 查找头部结束符 \r\n\r\n
            const headerEnd = this.buffer.indexOf('\r\n\r\n');
            if (headerEnd === -1) {
                return; // 头部不完整，等待更多数据
            }

            const headerStr = this.buffer.slice(0, headerEnd).toString('utf8');
            const contentLengthMatch = headerStr.match(/Content-Length:\s*(\d+)/i);
            if (!contentLengthMatch) {
                // 头部格式异常，丢弃这一行继续
                this.buffer = this.buffer.slice(headerEnd + 4);
                continue;
            }

            const contentLength = parseInt(contentLengthMatch[1], 10);
            const bodyStart = headerEnd + 4;
            const bodyEnd = bodyStart + contentLength;

            if (this.buffer.length < bodyEnd) {
                return; // body 不完整，等待更多数据
            }

            const bodyBuf = this.buffer.slice(bodyStart, bodyEnd);
            this.buffer = this.buffer.slice(bodyEnd);

            let msg;
            try {
                msg = JSON.parse(bodyBuf.toString('utf8'));
            } catch (e) {
                console.error(`[Lumyr LSP] JSON 解析失败: ${e.message}`);
                continue;
            }

            this._handleMessage(msg);
        }
    }

    /**
     * 处理从服务器收到的消息
     */
    _handleMessage(msg) {
        // 响应（有 id 且无 method）
        if (msg.id !== undefined && msg.method === undefined) {
            const pending = this.pendingRequests.get(msg.id);
            if (pending) {
                this.pendingRequests.delete(msg.id);
                if (msg.error) {
                    pending.reject(new Error(`LSP 错误 ${msg.error.code}: ${msg.error.message}`));
                } else {
                    pending.resolve(msg.result);
                }
            }
            return;
        }

        // 服务器主动推送的请求（如 window/workDoneProgress 等），此处忽略或简单响应
        if (msg.method && msg.id !== undefined) {
            // 服务器 -> 客户端请求，返回空结果
            this._write({ jsonrpc: '2.0', id: msg.id, result: null });
            return;
        }

        // 通知（有 method 无 id）
        if (msg.method) {
            this._handleNotification(msg.method, msg.params || {});
        }
    }

    _handleNotification(method, params) {
        switch (method) {
            case 'textDocument/publishDiagnostics':
                this._onPublishDiagnostics(params);
                break;
            default:
                console.log(`[Lumyr LSP] 未处理通知: ${method}`);
        }
    }

    _onPublishDiagnostics(params) {
        if (!this.diagnostics) return;
        const uri = vscode.Uri.parse(params.uri);
        const diagnostics = (params.diagnostics || []).map((d) => {
            const severityMap = {
                1: vscode.DiagnosticSeverity.Error,
                2: vscode.DiagnosticSeverity.Warning,
                3: vscode.DiagnosticSeverity.Information,
                4: vscode.DiagnosticSeverity.Hint
            };
            const diag = new vscode.Diagnostic(
                new vscode.Range(
                    new vscode.Position(d.range.start.line, d.range.start.character),
                    new vscode.Position(d.range.end.line, d.range.end.character)
                ),
                d.message,
                severityMap[d.severity] || vscode.DiagnosticSeverity.Error
            );
            diag.source = 'lumyr';
            return diag;
        });
        this.diagnostics.set(uri, diagnostics);
    }

    // ---- 文档同步 ----

    _didOpen(document) {
        this.sendNotification('textDocument/didOpen', {
            textDocument: {
                uri: document.uri.toString(),
                languageId: 'lumyr',
                version: document.version,
                text: document.getText()
            }
        });
    }

    _didChange(document, changes) {
        this.sendNotification('textDocument/didChange', {
            textDocument: {
                uri: document.uri.toString(),
                version: document.version
            },
            contentChanges: changes.map((c) => ({ text: c.text }))
        });
    }

    _didSave(document) {
        this.sendNotification('textDocument/didSave', {
            textDocument: { uri: document.uri.toString() }
        });
    }

    _didClose(document) {
        this.sendNotification('textDocument/didClose', {
            textDocument: { uri: document.uri.toString() }
        });
    }

    // ---- 对外接口（供 extension.js 注册 providers 调用） ----

    async requestHover(document, position) {
        const result = await this.sendRequest('textDocument/hover', {
            textDocument: { uri: document.uri.toString() },
            position: { line: position.line, character: position.character }
        });
        if (!result || !result.contents) return null;
        // MarkupContent 或字符串
        let contents;
        if (typeof result.contents === 'string') {
            contents = new vscode.MarkdownString(result.contents);
        } else if (result.contents.value !== undefined) {
            contents = new vscode.MarkdownString(result.contents.value);
        } else {
            return null;
        }
        return new vscode.Hover(contents);
    }

    async requestDefinition(document, position) {
        const result = await this.sendRequest('textDocument/definition', {
            textDocument: { uri: document.uri.toString() },
            position: { line: position.line, character: position.character }
        });
        if (!result) return null;
        const locs = Array.isArray(result) ? result : [result];
        return locs
            .filter((l) => l && l.uri && l.range)
            .map((l) => new vscode.Location(
                vscode.Uri.parse(l.uri),
                new vscode.Range(
                    new vscode.Position(l.range.start.line, l.range.start.character),
                    new vscode.Position(l.range.end.line, l.range.end.character)
                )
            ));
    }

    async requestCompletion(document, position) {
        const result = await this.sendRequest('textDocument/completion', {
            textDocument: { uri: document.uri.toString() },
            position: { line: position.line, character: position.character }
        });
        if (!result) return [];
        const items = Array.isArray(result) ? result : (result.items || []);
        return items.map((item) => {
            const ci = new vscode.CompletionItem(item.label);
            if (item.detail) ci.detail = item.detail;
            if (item.documentation) {
                ci.documentation = typeof item.documentation === 'string'
                    ? item.documentation
                    : (item.documentation.value || '');
            }
            if (item.kind !== undefined) {
                // LSP CompletionItemKind 与 VS Code 枚举值基本一致（数字）
                ci.kind = item.kind;
            }
            return ci;
        });
    }

    /**
     * 优雅关闭
     */
    async stop() {
        if (!this.process) return;
        try {
            if (this.initialized) {
                await this.sendRequest('shutdown', null);
                this.sendNotification('exit', {});
            }
        } catch (e) {
            console.warn(`[Lumyr LSP] 关闭时出错: ${e.message}`);
        }
        // 超时后强制杀死
        const proc = this.process;
        setTimeout(() => {
            if (!proc.killed) {
                try { proc.kill(); } catch (e) { /* ignore */ }
            }
        }, 2000);
        this.process = null;
        if (this.diagnostics) {
            this.diagnostics.dispose();
            this.diagnostics = null;
        }
    }
}

let client = null;
let disposables = [];

async function activate(context) {
    client = new LumyrLanguageClient();
    try {
        await client.start();
    } catch (err) {
        vscode.window.showErrorMessage(`Lumyr LSP 启动失败: ${err.message}`);
        return;
    }

    // 文档同步事件
    disposables.push(
        vscode.workspace.onDidOpenTextDocument((doc) => {
            if (doc.languageId === 'lumyr' || doc.fileName.endsWith('.lm')) {
                client._didOpen(doc);
            }
        })
    );

    disposables.push(
        vscode.workspace.onDidChangeTextDocument((event) => {
            const doc = event.document;
            if (doc.languageId === 'lumyr' || doc.fileName.endsWith('.lm')) {
                client._didChange(doc, event.contentChanges);
            }
        })
    );

    disposables.push(
        vscode.workspace.onDidSaveTextDocument((doc) => {
            if (doc.languageId === 'lumyr' || doc.fileName.endsWith('.lm')) {
                client._didSave(doc);
            }
        })
    );

    disposables.push(
        vscode.workspace.onDidCloseTextDocument((doc) => {
            if (doc.languageId === 'lumyr' || doc.fileName.endsWith('.lm')) {
                client._didClose(doc);
            }
        })
    );

    // Hover
    disposables.push(
        vscode.languages.registerHoverProvider('lumyr', {
            async provideHover(document, position, token) {
                try {
                    return await client.requestHover(document, position);
                } catch (e) {
                    console.warn(`[Lumyr LSP] hover 失败: ${e.message}`);
                    return null;
                }
            }
        })
    );

    // Definition
    disposables.push(
        vscode.languages.registerDefinitionProvider('lumyr', {
            async provideDefinition(document, position, token) {
                try {
                    return await client.requestDefinition(document, position);
                } catch (e) {
                    console.warn(`[Lumyr LSP] definition 失败: ${e.message}`);
                    return null;
                }
            }
        })
    );

    // Completion
    disposables.push(
        vscode.languages.registerCompletionItemProvider(
            'lumyr',
            {
                async provideCompletionItems(document, position, token) {
                    try {
                        return await client.requestCompletion(document, position);
                    } catch (e) {
                        console.warn(`[Lumyr LSP] completion 失败: ${e.message}`);
                        return [];
                    }
                }
            },
            '.', '('
        )
    );

    context.subscriptions.push(...disposables);
    console.log('[Lumyr LSP] 扩展已激活');
}

function deactivate() {
    if (client) {
        client.stop();
        client = null;
    }
    disposables = [];
}

module.exports = { activate, deactivate };
