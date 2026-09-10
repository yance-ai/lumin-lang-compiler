"""
lumyr_lsp.documents
====================

文本文档管理器。

维护编辑器中打开的所有文档，支持增量/全量文本修改，
以及行/列位置与字符偏移之间的双向转换。

位置编码说明
------------
LSP 规范规定 position 中的 ``character`` 字段以 **UTF-16 编码单元** 为单位。
本实现采用与 UTF-16 兼容的字符索引转换策略：

- 对于 BMP 平面字符（绝大多数编程语言的标识符、ASCII 字符），
  一个 Unicode 码点 = 一个 UTF-16 编码单元，转换无差异。
- 对于 BMP 以外的字符（如 emoji、生僻汉字扩展区），
  本实现会正确计算 surrogate pair 的长度（2 个 UTF-16 单元）。

核心转换函数 ``_utf16_units_up_to`` 和 ``_utf16_offset_to_index`` 实现了
精确的 UTF-16 编码单元计数。
"""

from __future__ import annotations

import re
from typing import Optional, Tuple

from .protocol import (
    Position,
    Range,
    TextDocumentContentChangeEvent,
    TextDocumentItem,
)


# ---------------------------------------------------------------------------
# UTF-16 编码单元工具函数
# ---------------------------------------------------------------------------

def _utf16_len(ch: str) -> int:
    """返回单个字符的 UTF-16 编码单元数。

    BMP 平面（U+0000 ~ U+FFFF）：1 个 UTF-16 编码单元。
    补充平面（U+10000 ~ U+10FFFF）：2 个 UTF-16 编码单元（surrogate pair）。

    Python 的 ord() 返回 Unicode 码点值，据此判断是否在 BMP 内。
    """
    cp = ord(ch)
    return 1 if cp <= 0xFFFF else 2


def _line_utf16_to_index(line_text: str, utf16_pos: int) -> int:
    """将一行文本中的 UTF-16 位置转换为 Python 字符串索引。

    从行首开始逐字符遍历，累计 UTF-16 编码单元数，
    直到累计值达到或超过目标位置。

    Args:
        line_text: 单行文本（不含换行符）
        utf16_pos: 目标 UTF-16 编码单元位置（从 0 开始）

    Returns:
        对应的 Python 字符串字符索引
    """
    accumulated = 0
    for i, ch in enumerate(line_text):
        if accumulated >= utf16_pos:
            return i
        accumulated += _utf16_len(ch)
    # 目标位置超出行尾，返回行尾
    return len(line_text)


def _line_index_to_utf16(line_text: str, char_index: int) -> int:
    """将一行文本中的 Python 字符串索引转换为 UTF-16 编码单元位置。

    Args:
        line_text: 单行文本（不含换行符）
        char_index: Python 字符串字符索引

    Returns:
        对应的 UTF-16 编码单元位置
    """
    if char_index <= 0:
        return 0
    char_index = min(char_index, len(line_text))
    accumulated = 0
    for i, ch in enumerate(line_text):
        if i >= char_index:
            break
        accumulated += _utf16_len(ch)
    return accumulated


# ---------------------------------------------------------------------------
# TextDocument —— 单个文本文档
# ---------------------------------------------------------------------------

class TextDocument:
    """编辑器中的一个文本文档。

    维护文档的 URI、语言 ID、版本号和全文文本。
    支持增量修改（apply_change）和位置↔偏移转换。

    行分隔符统一为 \\n。内部使用 text.split("\\n") 维护行列表，
    在修改时重新切分。
    """

    def __init__(self, uri: str, language_id: str, version: int, text: str):
        self.uri: str = uri
        self.language_id: str = language_id
        self.version: int = version
        self.text: str = text

    # -- 行操作 ---------------------------------------------------------------

    def line_at(self, line: int) -> str:
        """返回指定行的文本（不含换行符）。

        行号从 0 开始。越界返回空字符串。
        """
        lines = self.text.split("\n")
        if 0 <= line < len(lines):
            return lines[line]
        return ""

    def line_count(self) -> int:
        """返回文档总行数。"""
        return len(self.text.split("\n"))

    # -- 位置 ↔ 偏移转换 ------------------------------------------------------

    def position_to_offset(self, position: Position) -> int:
        """将 (line, character) 位置转换为 text 中的字符偏移量。

        算法：
        1. 将 text 按 \\n 切分为行列表。
        2. 累加 position.line 之前所有行的长度（+1 用于换行符），
           得到目标行的起始偏移。
        3. 在目标行内，将 UTF-16 character 位置转换为字符索引。
        4. 两者相加得到最终偏移。

        Args:
            position: LSP 位置（line 和 character 均从 0 开始）

        Returns:
            text 中的字符索引（0-based）
        """
        lines = self.text.split("\n")
        line = position.line
        character = position.character

        # 计算到目标行为止的偏移
        offset = 0
        for i in range(line):
            if i < len(lines):
                offset += len(lines[i]) + 1  # +1 是换行符

        # 在目标行内转换 UTF-16 位置 → 字符索引
        if line < len(lines):
            line_text = lines[line]
            char_idx = _line_utf16_to_index(line_text, character)
        else:
            char_idx = 0

        return offset + char_idx

    def offset_to_position(self, offset: int) -> Position:
        """将 text 中的字符偏移量转换为 (line, character) 位置。

        算法：
        1. 从 offset 向前数换行符，确定行号。
        2. 在该行内，将字符索引转换为 UTF-16 编码单元位置。

        Args:
            offset: text 中的字符索引（0-based）

        Returns:
            LSP Position
        """
        # 越界保护
        if offset < 0:
            offset = 0
        if offset > len(self.text):
            offset = len(self.text)

        # 计算行号和行内偏移
        line = 0
        line_start = 0
        for i in range(offset + 1):
            if i < len(self.text) and self.text[i] == "\n":
                line += 1
                line_start = i + 1

        # 行内字符索引
        char_index_in_line = offset - line_start

        # 转换为 UTF-16 编码单元位置
        lines = self.text.split("\n")
        if line < len(lines):
            line_text = lines[line]
            utf16_pos = _line_index_to_utf16(line_text, char_index_in_line)
        else:
            utf16_pos = 0

        return Position(line=line, character=utf16_pos)

    # -- 文本修改 -------------------------------------------------------------

    def apply_change(self, change: TextDocumentContentChangeEvent) -> None:
        """应用文本变更。

        如果 change.range 为 None → 全量替换整个 text。
        否则 → 增量替换：计算 range 对应的字符偏移，替换该区间文本。

        增量替换算法：
        1. start_offset = position_to_offset(change.range.start)
        2. end_offset = position_to_offset(change.range.end)
        3. new_text = text[:start_offset] + change.text + text[end_offset:]

        Args:
            change: 文本变更事件
        """
        if change.range is None:
            # 全量替换
            self.text = change.text
            return

        # 增量替换
        start_offset = self.position_to_offset(change.range.start)
        end_offset = self.position_to_offset(change.range.end)

        # 安全检查：确保 start <= end
        if start_offset > end_offset:
            start_offset, end_offset = end_offset, start_offset

        self.text = self.text[:start_offset] + change.text + self.text[end_offset:]

    # -- 标识符提取 -----------------------------------------------------------

    # 标识符正则：字母或下划线开头，后跟字母/数字/下划线
    _IDENT_RE = re.compile(r'[a-zA-Z_][a-zA-Z0-9_]*')

    def word_at_position(
        self, position: Position
    ) -> Optional[Tuple[str, Position, Position]]:
        """获取光标位置处的标识符及其起止位置。

        从光标位置向左扩展找到标识符起点，向右扩展找到终点。
        标识符定义为 [a-zA-Z_][a-zA-Z0-9_]*。

        Args:
            position: 光标位置

        Returns:
            (word_text, start_position, end_position) 三元组；
            如果光标不在标识符上则返回 None。
        """
        line_text = self.line_at(position.line)
        if not line_text:
            return None

        # 将 UTF-16 character 位置转为行内字符索引
        char_idx = _line_utf16_to_index(line_text, position.character)

        # 向左扩展找标识符起点
        start_idx = char_idx
        while start_idx > 0 and self._is_identifier_char(line_text[start_idx - 1]):
            start_idx -= 1

        # 向右扩展找标识符终点
        end_idx = char_idx
        line_len = len(line_text)
        while end_idx < line_len and self._is_identifier_char(line_text[end_idx]):
            end_idx += 1

        if start_idx == end_idx:
            return None  # 光标不在标识符上

        word = line_text[start_idx:end_idx]

        # 验证：必须以字母或下划线开头
        if not word or not (word[0].isalpha() or word[0] == "_"):
            return None

        # 转换起止位置为 LSP Position（UTF-16）
        start_char = _line_index_to_utf16(line_text, start_idx)
        end_char = _line_index_to_utf16(line_text, end_idx)

        start_pos = Position(line=position.line, character=start_char)
        end_pos = Position(line=position.line, character=end_char)

        return (word, start_pos, end_pos)

    @staticmethod
    def _is_identifier_char(ch: str) -> bool:
        """判断字符是否是标识符的组成部分。"""
        return ch.isalnum() or ch == "_"


# ---------------------------------------------------------------------------
# DocumentManager —— 文档管理器
# ---------------------------------------------------------------------------

class DocumentManager:
    """管理所有打开的文本文档。

    维护 uri → TextDocument 的映射，处理打开/修改/保存/关闭事件。
    """

    def __init__(self):
        self.documents: dict[str, TextDocument] = {}

    def open(self, item: TextDocumentItem) -> TextDocument:
        """打开文档（对应 textDocument/didOpen 通知）。

        Args:
            item: 完整的文本文档项

        Returns:
            新创建的 TextDocument
        """
        doc = TextDocument(
            uri=item.uri,
            language_id=item.languageId,
            version=item.version,
            text=item.text,
        )
        self.documents[item.uri] = doc
        return doc

    def change(
        self,
        uri: str,
        version: int,
        changes: list,  # list[TextDocumentContentChangeEvent]
    ) -> None:
        """应用文档变更（对应 textDocument/didChange 通知）。

        按顺序应用所有变更事件，然后更新文档版本号。

        Args:
            uri: 文档 URI
            version: 新版本号
            changes: 变更事件列表
        """
        doc = self.documents.get(uri)
        if doc is None:
            return

        for change in changes:
            doc.apply_change(change)

        doc.version = version

    def close(self, uri: str) -> None:
        """关闭文档（对应 textDocument/didClose 通知）。

        从管理器中移除该文档。
        """
        self.documents.pop(uri, None)

    def save(self, uri: str, text: Optional[str] = None) -> None:
        """保存文档（对应 textDocument/didSave 通知）。

        Args:
            uri: 文档 URI
            text: 保存时的完整文本。如果为 None，则保留当前内容不变。
        """
        doc = self.documents.get(uri)
        if doc is None:
            return
        if text is not None:
            doc.text = text

    def get(self, uri: str) -> Optional[TextDocument]:
        """获取指定 URI 的文档。"""
        return self.documents.get(uri)
