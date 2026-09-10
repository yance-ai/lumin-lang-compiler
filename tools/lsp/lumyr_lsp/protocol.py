"""
lumyr_lsp.protocol
==================

LSP 协议类型定义和 JSON-RPC 消息处理。

本模块定义了 LSP (Language Server Protocol) 中使用的核心数据结构，
以 Python dataclass 形式表示，并提供 JSON-RPC 序列化/反序列化工具函数。

所有类型严格遵循 LSP 3.17 规范（https://microsoft.github.io/language-server-protocol/）。
"""

from __future__ import annotations

import json
import struct
import sys
from dataclasses import dataclass, field, fields, is_dataclass
from typing import Any, Optional, Union, get_type_hints


# ---------------------------------------------------------------------------
# 常量枚举（使用 int 子类模拟 LSP 规范中的数值枚举）
# ---------------------------------------------------------------------------

class DiagnosticSeverity:
    """诊断严重程度（LSP 规范 §DiagnosticSeverity）。"""
    Error = 1       # 错误
    Warning = 2     # 警告
    Information = 3 # 信息
    Hint = 4        # 提示


class CompletionItemKind:
    """补全项类型（LSP 规范 §CompletionItemKind）。"""
    Text = 1
    Method = 2
    Function = 3
    Constructor = 4
    Field = 5
    Variable = 6
    Class = 7
    Interface = 8
    Module = 9
    Property = 10
    Unit = 11
    Value = 12
    Enum = 13
    Keyword = 14
    Snippet = 15
    Color = 16
    File = 17
    Reference = 18
    Folder = 19
    EnumMember = 20
    Constant = 21
    Struct = 22
    Event = 23
    Operator = 24
    TypeParameter = 25


class SymbolKind:
    """文档符号类型（LSP 规范 §SymbolKind）。"""
    File = 1
    Module = 2
    Namespace = 3
    Package = 4
    Class = 5
    Method = 6
    Property = 7
    Field = 8
    Constructor = 9
    Enum = 10
    Interface = 11
    Function = 12
    Variable = 13
    Constant = 14
    String = 15
    Number = 16
    Boolean = 17
    Array = 18
    Object = 19
    Key = 20
    Null = 21
    EnumMember = 22
    Struct = 23
    Event = 24
    Operator = 25
    TypeParameter = 26


# ---------------------------------------------------------------------------
# 核心数据结构（dataclass）
# ---------------------------------------------------------------------------

@dataclass
class Position:
    """文档中的一个位置（零起始行/列）。"""
    line: int
    character: int


@dataclass
class Range:
    """文档中的一个连续范围。"""
    start: Position
    end: Position


@dataclass
class TextDocumentIdentifier:
    """文本文档标识符。"""
    uri: str


@dataclass
class TextDocumentItem:
    """完整的文本文档项（用于 didOpen 通知）。"""
    uri: str
    languageId: str
    version: int
    text: str


@dataclass
class VersionedTextDocumentIdentifier:
    """带版本号的文本文档标识符。"""
    uri: str
    version: int


@dataclass
class TextDocumentContentChangeEvent:
    """文本内容变更事件。

    如果 ``range`` 为 None，表示全量替换整个文档内容；
    否则为增量修改，仅替换 range 范围内的文本。
    """
    range: Optional[Range]
    rangeLength: Optional[int]
    text: str


@dataclass
class MarkupContent:
    """带标记语言类型的内容。"""
    kind: str   # "plaintext" 或 "markdown"
    value: str


@dataclass
class Hover:
    """悬停信息。"""
    contents: Union[str, MarkupContent]
    range: Optional[Range] = None


@dataclass
class Location:
    """文档中的一个位置范围。"""
    uri: str
    range: Range


@dataclass
class CompletionItem:
    """补全建议项。"""
    label: str
    kind: int
    detail: Optional[str] = None
    documentation: Optional[str] = None
    insertText: Optional[str] = None


@dataclass
class CompletionList:
    """补全结果列表。"""
    isIncomplete: bool
    items: list


@dataclass
class Diagnostic:
    """诊断信息（错误/警告等）。"""
    range: Range
    severity: int
    code: Optional[Union[str, int]] = None
    source: Optional[str] = None
    message: str = ""


@dataclass
class PublishDiagnosticsParams:
    """发布诊断的参数。"""
    uri: str
    diagnostics: list


@dataclass
class TextDocumentPositionParams:
    """文本文档 + 位置参数（用于 hover/definition/completion 等请求）。"""
    textDocument: TextDocumentIdentifier
    position: Position


@dataclass
class CompletionParams:
    """补全请求参数。"""
    textDocument: TextDocumentIdentifier
    position: Position
    context: Optional[dict] = None


# ---------------------------------------------------------------------------
# JSON 序列化工具
# ---------------------------------------------------------------------------

def to_json(obj: Any) -> Any:
    """将 dataclass / 嵌套结构递归转为 dict。

    规则：
    - dataclass 实例 → dict（None 值跳过）
    - list / tuple → list（递归处理每个元素）
    - dict → dict（递归处理每个值）
    - 其他类型 → 原样返回

    用于将 LSP 响应对象序列化为 JSON-RPC params。
    """
    if obj is None:
        return None

    # dataclass 实例
    if is_dataclass(obj) and not isinstance(obj, type):
        result = {}
        for f in fields(obj):
            value = getattr(obj, f.name)
            if value is not None:
                result[f.name] = to_json(value)
        return result

    # 列表或元组
    if isinstance(obj, (list, tuple)):
        return [to_json(item) for item in obj]

    # 字典
    if isinstance(obj, dict):
        return {k: to_json(v) for k, v in obj.items() if v is not None}

    # 基本类型（str, int, float, bool）原样返回
    return obj


def from_json(cls: type, data: dict) -> Any:
    """从 dict 递归构造 dataclass 实例。

    根据 cls 的类型注解自动转换嵌套的 dataclass、Optional、Union 等。

    用法：
        pos = from_json(Position, {"line": 0, "character": 5})
    """
    if data is None:
        return None

    if not is_dataclass(cls):
        return data

    hints = get_type_hints(cls)
    kwargs = {}

    for f in fields(cls):
        key = f.name
        if key not in data:
            # 字段缺失：使用默认值
            continue

        raw_value = data[key]
        expected_type = hints.get(f.name, f.type)

        # 递归转换嵌套的 dataclass
        kwargs[key] = _convert_value(raw_value, expected_type)

    return cls(**kwargs)


def _convert_value(raw: Any, expected_type: Any) -> Any:
    """根据期望类型转换原始 JSON 值。"""
    if raw is None:
        return None

    # 处理 Optional / Union —— 直接返回原始值，让 Python 动态处理
    # （dataclass 不做运行时类型检查）
    if isinstance(expected_type, str):
        # 前向引用，简化处理
        return raw

    # 如果期望类型是一个 dataclass
    if is_dataclass(expected_type) and not isinstance(expected_type, type):
        # expected_type 是 dataclass 类本身
        pass
    if isinstance(expected_type, type) and is_dataclass(expected_type):
        if isinstance(raw, dict):
            return from_json(expected_type, raw)
        return raw

    # 处理 list[SomeDataclass] —— 简化：不深度转换，返回原始 list
    # （在实际使用中，documents/server 模块会手动解析这些字段）
    return raw


# ---------------------------------------------------------------------------
# JSON-RPC 传输层工具
# ---------------------------------------------------------------------------

def parse_content_length_header(stream) -> int:
    """从流中读取 LSP 头部，解析 Content-Length。

    LSP 协议要求每条消息以 ``Content-Length: <N>\\r\\n\\r\\n`` 开头，
    后跟 N 字节的 UTF-8 JSON body。

    本函数逐字节读取头部行，直到遇到空行（\\r\\n），
    然后返回 body 的字节长度。

    Args:
        stream: 可读的二进制流（如 sys.stdin.buffer）

    Returns:
        body 的字节长度（int）

    Raises:
        EOFError: 流在读取到完整头部前关闭
        ValueError: 头部中缺少 Content-Length 字段
    """
    content_length = 0
    # 逐行读取头部（每行以 \r\n 结尾）
    # 头部结束标志：一个空行（即 \r\n）
    while True:
        line = _read_line(stream)
        if line == b"":
            # EOF
            raise EOFError("Stream closed while reading LSP header")

        # line 已经去掉了尾部的 \r\n
        if line == b"":
            # 空行 —— 头部结束
            break

        # 解析 "Header-Name: value" 格式
        try:
            header_str = line.decode("ascii")
        except UnicodeDecodeError:
            continue  # 跳过非 ASCII 行

        if ":" in header_str:
            name, _, value = header_str.partition(":")
            name = name.strip().lower()
            value = value.strip()
            if name == "content-length":
                content_length = int(value)
            # Content-Type 等其他头部忽略

    return content_length


def _read_line(stream) -> bytes:
    """从二进制流中读取一行（以 \\r\\n 结尾），返回不带换行符的字节。

    逐字节读取直到遇到 \\n，然后去掉尾部的 \\r\\n。
    如果遇到 EOF 返回已读取的内容（可能为空）。
    """
    buf = bytearray()
    while True:
        byte = stream.read(1)
        if not byte:
            # EOF
            return bytes(buf)
        if byte == b"\n":
            # 去掉尾部的 \r
            if buf and buf[-1:] == b"\r":
                buf.pop()
            return bytes(buf)
        buf.extend(byte)


def encode_message(message: dict) -> bytes:
    """将 JSON-RPC 消息编码为带 Content-Length 头部的二进制数据。

    Args:
        message: JSON-RPC 消息 dict

    Returns:
        完整的二进制消息（头部 + body）
    """
    body = json.dumps(message, ensure_ascii=False).encode("utf-8")
    header = f"Content-Length: {len(body)}\r\n\r\n".encode("ascii")
    return header + body
