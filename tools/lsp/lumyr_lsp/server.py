"""
lumyr_lsp.server
=================

LSP 服务器核心。

负责：
1. JSON-RPC 消息收发（基于 stdin/stdout 的 Content-Length 帧协议）
2. LSP 方法分发（initialize / shutdown / didOpen / hover 等）
3. 外部 handler 注册（hover / definition / completion / didSave）
4. 诊断信息发布（publishDiagnostics）

传输层：stdin/stdout 均使用二进制模式（buffer），
避免 Windows 控制台编码导致的乱码。
"""

from __future__ import annotations

import json
import sys
from typing import Any, Callable, Optional, Union

from .documents import DocumentManager, TextDocument
from .protocol import (
    CompletionList,
    Diagnostic,
    Hover,
    Location,
    Position,
    Range,
    TextDocumentContentChangeEvent,
    TextDocumentItem,
    encode_message,
    parse_content_length_header,
    to_json,
)


# JSON-RPC 错误码（LSP 规范 §ErrorCodes）
class JsonRpcError:
    ParseError = -32700
    InvalidRequest = -32600
    MethodNotFound = -32601
    InvalidParams = -32602
    InternalError = -32603


# TextDocumentSyncKind 常量
class TextDocumentSyncKind:
    None_ = 0
    Full = 1
    Incremental = 2


# ---------------------------------------------------------------------------
# LspServer —— 服务器核心
# ---------------------------------------------------------------------------

class LspServer:
    """LSP 语言服务器。

    生命周期：
    1. __init__() —— 初始化文档管理器和 handler 槽位
    2. 调用 on_hover / on_definition / on_completion / on_did_save 注册功能 handler
    3. run() —— 进入主循环，处理 JSON-RPC 消息
    4. 收到 shutdown → 标记 shutting_down
    5. 收到 exit → 退出进程

    外部功能模块（features）通过注册 handler 来提供：
    - 悬停信息（hover）
    - 跳转定义（definition）
    - 代码补全（completion）
    - 保存后重新分析（didSave）
    """

    def __init__(self):
        self.documents = DocumentManager()

        # 外部 handler 槽位（初始为 None，由 features 模块注册）
        self._hover_handler: Optional[Callable] = None
        self._definition_handler: Optional[Callable] = None
        self._completion_handler: Optional[Callable] = None
        self._did_save_handler: Optional[Callable] = None

        # 请求 ID 计数器（服务器主动发起请求时使用，当前未使用）
        self._request_id = 0

        # 关闭标志
        self._shutting_down = False

    # -- Handler 注册接口 -----------------------------------------------------

    def on_hover(self, handler: Callable) -> None:
        """注册悬停 handler。

        Handler 签名: (server, uri: str, position: Position) -> Hover | None
        """
        self._hover_handler = handler

    def on_definition(self, handler: Callable) -> None:
        """注册跳转定义 handler。

        Handler 签名: (server, uri: str, position: Position) -> Location | list[Location] | None
        """
        self._definition_handler = handler

    def on_completion(self, handler: Callable) -> None:
        """注册补全 handler。

        Handler 签名: (server, uri: str, position: Position) -> CompletionList | list[CompletionItem] | None
        """
        self._completion_handler = handler

    def on_did_save(self, handler: Callable) -> None:
        """注册保存 handler（用于触发重新分析和发布诊断）。

        Handler 签名: (server, uri: str)
        """
        self._did_save_handler = handler

    # -- 消息发送 -------------------------------------------------------------

    def send_notification(self, method: str, params: dict) -> None:
        """发送 JSON-RPC 通知（无 id，不需要响应）。

        Args:
            method: 通知方法名（如 "textDocument/publishDiagnostics"）
            params: 通知参数（dict）
        """
        message = {
            "jsonrpc": "2.0",
            "method": method,
            "params": params,
        }
        self._write_message(message)

    def send_response(self, request_id: Any, result: Any) -> None:
        """发送 JSON-RPC 成功响应。

        Args:
            request_id: 原始请求的 id
            result: 响应结果（将被 to_json 序列化）
        """
        message = {
            "jsonrpc": "2.0",
            "id": request_id,
            "result": to_json(result),
        }
        self._write_message(message)

    def send_error(self, request_id: Any, code: int, message: str) -> None:
        """发送 JSON-RPC 错误响应。

        Args:
            request_id: 原始请求的 id
            code: 错误码
            message: 错误消息
        """
        error_body = {
            "code": code,
            "message": message,
        }
        message_obj = {
            "jsonrpc": "2.0",
            "id": request_id,
            "error": error_body,
        }
        self._write_message(message_obj)

    def publish_diagnostics(self, uri: str, diagnostics: list) -> None:
        """发布诊断信息到客户端。

        发送 textDocument/publishDiagnostics 通知。
        如果 diagnostics 为空列表，则清除该文件的诊断。

        Args:
            uri: 文档 URI
            diagnostics: Diagnostic 对象列表
        """
        params = {
            "uri": uri,
            "diagnostics": to_json(diagnostics),
        }
        self.send_notification("textDocument/publishDiagnostics", params)

    # -- 底层写入 -------------------------------------------------------------

    def _write_message(self, message: dict) -> None:
        """将 JSON-RPC 消息编码并写入 stdout。

        使用 sys.stdout.buffer 二进制写入，确保 Windows 兼容。
        写入后必须 flush，否则客户端可能收不到消息。
        """
        data = encode_message(message)
        sys.stdout.buffer.write(data)
        sys.stdout.buffer.flush()

    # -- 主循环 ---------------------------------------------------------------

    def run(self) -> None:
        """主循环：读取 JSON-RPC 消息，分发处理，发送响应。

        循环流程：
        1. 读取 Content-Length 头部 → 获取 body 字节数 N
        2. 读取 N 字节 body → 解析为 JSON dict
        3. 根据是否有 "id" 字段判断是请求还是通知
        4. 调用对应 handler 处理
        5. 请求 → 发送响应；通知 → 无响应
        6. 收到 exit 通知时退出循环

        读取从 sys.stdin.buffer 进行（二进制模式）。
        """
        stdin = sys.stdin.buffer

        while True:
            try:
                # 1. 读取头部，获取 body 长度
                content_length = parse_content_length_header(stdin)

                # 2. 读取 body
                body = self._read_exact(stdin, content_length)

                # 3. 解析 JSON
                message = json.loads(body.decode("utf-8"))

            except EOFError:
                # 标准输入关闭，退出
                break
            except json.JSONDecodeError:
                self.send_error(None, JsonRpcError.ParseError, "Parse error")
                continue
            except Exception as e:
                # 读取过程中出错，尝试继续
                sys.stderr.write(f"[lumyr-lsp] Read error: {e}\n")
                sys.stderr.flush()
                continue

            # 4. 分发消息
            try:
                self._dispatch(message)
            except Exception as e:
                sys.stderr.write(f"[lumyr-lsp] Dispatch error: {e}\n")
                sys.stderr.flush()
                # 如果是请求且出错，返回内部错误
                if "id" in message:
                    self.send_error(
                        message["id"],
                        JsonRpcError.InternalError,
                        str(e),
                    )

    @staticmethod
    def _read_exact(stream, n: int) -> bytes:
        """从流中精确读取 n 字节。

        如果 EOF 在读取完 n 字节前到达，抛出 EOFError。
        """
        chunks = []
        remaining = n
        while remaining > 0:
            chunk = stream.read(remaining)
            if not chunk:
                raise EOFError("Stream closed while reading body")
            chunks.append(chunk)
            remaining -= len(chunk)
        return b"".join(chunks)

    # -- 消息分发 -------------------------------------------------------------

    def _dispatch(self, message: dict) -> None:
        """分发 JSON-RPC 消息到对应的处理函数。

        Args:
            message: 已解析的 JSON-RPC 消息 dict
        """
        method = message.get("method", "")
        params = message.get("params", {})
        request_id = message.get("id")  # None 表示通知

        # 查找处理函数
        handler = self._method_handlers().get(method)

        if handler is None:
            # 未知方法
            if request_id is not None:
                self.send_error(
                    request_id,
                    JsonRpcError.MethodNotFound,
                    f"Method not found: {method}",
                )
            # 通知类未知方法：直接忽略
            return

        # 调用处理函数（handler 已是 bound method，无需再传 self）
        handler(request_id, params)

    # -- 方法路由表 -----------------------------------------------------------

    def _method_handlers(self) -> dict:
        """返回 LSP 方法名 → 处理函数的映射表。"""
        return {
            # 生命周期
            "initialize": self._handle_initialize,
            "initialized": self._handle_initialized,
            "shutdown": self._handle_shutdown,
            "exit": self._handle_exit,

            # 文档同步
            "textDocument/didOpen": self._handle_did_open,
            "textDocument/didChange": self._handle_did_change,
            "textDocument/didSave": self._handle_did_save,
            "textDocument/didClose": self._handle_did_close,

            # 语言功能
            "textDocument/hover": self._handle_hover,
            "textDocument/definition": self._handle_definition,
            "textDocument/completion": self._handle_completion,
        }

    # -- 生命周期处理 ---------------------------------------------------------

    def _handle_initialize(self, request_id, params):
        """处理 initialize 请求。

        返回服务器信息和能力声明。
        """
        result = {
            "capabilities": {
                # 文本同步：增量同步（2 = Incremental）
                "textDocumentSync": TextDocumentSyncKind.Incremental,
                # 悬停
                "hoverProvider": True,
                # 跳转定义
                "definitionProvider": True,
                # 代码补全
                "completionProvider": {
                    "triggerCharacters": [".", "("],
                },
            },
            "serverInfo": {
                "name": "lumyr-lsp",
                "version": "0.1.0",
            },
        }
        self.send_response(request_id, result)

    def _handle_initialized(self, request_id, params):
        """处理 initialized 通知（无操作）。"""
        # 客户端已完成初始化，无需响应
        pass

    def _handle_shutdown(self, request_id, params):
        """处理 shutdown 请求。

        设置关闭标志，返回 None。
        之后客户端应发送 exit 通知来终止进程。
        """
        self._shutting_down = True
        self.send_response(request_id, None)

    def _handle_exit(self, request_id, params):
        """处理 exit 通知。

        退出进程：
        - 如果之前收到过 shutdown → exit code 0
        - 否则 → exit code 1
        """
        code = 0 if self._shutting_down else 1
        sys.exit(code)

    # -- 文档同步处理 ---------------------------------------------------------

    def _handle_did_open(self, request_id, params):
        """处理 textDocument/didOpen 通知。"""
        doc_info = params.get("textDocument", {})
        item = TextDocumentItem(
            uri=doc_info.get("uri", ""),
            languageId=doc_info.get("languageId", ""),
            version=doc_info.get("version", 0),
            text=doc_info.get("text", ""),
        )
        self.documents.open(item)

    def _handle_did_change(self, request_id, params):
        """处理 textDocument/didChange 通知。"""
        doc_id = params.get("textDocument", {})
        uri = doc_id.get("uri", "")
        version = doc_id.get("version", 0)

        raw_changes = params.get("contentChanges", [])
        changes = []
        for rc in raw_changes:
            range_raw = rc.get("range")
            change_range = None
            if range_raw is not None:
                start = range_raw.get("start", {})
                end = range_raw.get("end", {})
                change_range = Range(
                    start=Position(
                        line=start.get("line", 0),
                        character=start.get("character", 0),
                    ),
                    end=Position(
                        line=end.get("line", 0),
                        character=end.get("character", 0),
                    ),
                )
            change = TextDocumentContentChangeEvent(
                range=change_range,
                rangeLength=rc.get("rangeLength"),
                text=rc.get("text", ""),
            )
            changes.append(change)

        self.documents.change(uri, version, changes)

    def _handle_did_save(self, request_id, params):
        """处理 textDocument/didSave 通知。

        先保存文档，然后触发外部注册的 didSave handler（用于重新分析）。
        """
        doc_id = params.get("textDocument", {})
        uri = doc_id.get("uri", "")
        text = params.get("text")  # 可选：保存时的完整文本

        self.documents.save(uri, text)

        # 触发外部 handler（重新分析 → 发布诊断）
        if self._did_save_handler is not None:
            try:
                self._did_save_handler(self, uri)
            except Exception as e:
                sys.stderr.write(f"[lumyr-lsp] didSave handler error: {e}\n")
                sys.stderr.flush()

    def _handle_did_close(self, request_id, params):
        """处理 textDocument/didClose 通知。

        关闭文档，并发布空诊断列表清除该文件的所有诊断。
        """
        doc_id = params.get("textDocument", {})
        uri = doc_id.get("uri", "")

        self.documents.close(uri)

        # 清除该文件的诊断
        self.publish_diagnostics(uri, [])

    # -- 语言功能处理 ---------------------------------------------------------

    def _handle_hover(self, request_id, params):
        """处理 textDocument/hover 请求。"""
        if self._hover_handler is None:
            self.send_response(request_id, None)
            return

        uri = params.get("textDocument", {}).get("uri", "")
        pos_raw = params.get("position", {})
        position = Position(
            line=pos_raw.get("line", 0),
            character=pos_raw.get("character", 0),
        )

        try:
            result = self._hover_handler(self, uri, position)
            self.send_response(request_id, result)
        except Exception as e:
            sys.stderr.write(f"[lumyr-lsp] hover handler error: {e}\n")
            sys.stderr.flush()
            self.send_response(request_id, None)

    def _handle_definition(self, request_id, params):
        """处理 textDocument/definition 请求。"""
        if self._definition_handler is None:
            self.send_response(request_id, None)
            return

        uri = params.get("textDocument", {}).get("uri", "")
        pos_raw = params.get("position", {})
        position = Position(
            line=pos_raw.get("line", 0),
            character=pos_raw.get("character", 0),
        )

        try:
            result = self._definition_handler(self, uri, position)
            self.send_response(request_id, result)
        except Exception as e:
            sys.stderr.write(f"[lumyr-lsp] definition handler error: {e}\n")
            sys.stderr.flush()
            self.send_response(request_id, None)

    def _handle_completion(self, request_id, params):
        """处理 textDocument/completion 请求。"""
        if self._completion_handler is None:
            self.send_response(request_id, None)
            return

        uri = params.get("textDocument", {}).get("uri", "")
        pos_raw = params.get("position", {})
        position = Position(
            line=pos_raw.get("line", 0),
            character=pos_raw.get("character", 0),
        )

        try:
            result = self._completion_handler(self, uri, position)
            # 如果 handler 返回 list[CompletionItem]，包装为 CompletionList
            if isinstance(result, list):
                result = CompletionList(isIncomplete=False, items=result)
            self.send_response(request_id, result)
        except Exception as e:
            sys.stderr.write(f"[lumyr-lsp] completion handler error: {e}\n")
            sys.stderr.flush()
            self.send_response(request_id, None)
