# -*- coding: utf-8 -*-
"""
Lumyr LSP 功能模块。

在 register_features(server) 中把 hover / goto-definition / completion /
did-save 处理器注册到 LspServer。模块自身不自动注册（避免循环导入），
由 __main__ 在创建 server 实例后调用。

诊断发布策略：
  - 首次分析某文档（缓存未命中）时自动 publish_diagnostics；
  - 后续 didChange 导致 doc.version 变化时，_get_analysis 检测到版本号
    不一致，自动重新分析并发布新的诊断；
  - didSave 时强制重新分析并发布诊断（确保与磁盘内容一致）。
"""

from __future__ import annotations

from .server import LspServer
from .analysis import LumyrAnalyzer, KEYWORDS
from .protocol import (
    Hover,
    MarkupContent,
    Location,
    Range,
    Position,
    CompletionItem,
    CompletionList,
    Diagnostic,
    DiagnosticSeverity,
)

# 全局分析器与缓存：uri -> {"version": int, "result": AnalysisResult}
_analyzer = LumyrAnalyzer()
_cache: dict[str, dict] = {}


def _get_analysis(server: LspServer, uri: str):
    """获取或重新计算文档的分析结果。

    版本号未变则复用缓存；版本变化（含首次打开）时重新分析并发布诊断。
    """
    doc = server.documents.get(uri)
    if doc is None:
        return None

    # 版本未变且缓存存在 -> 直接复用
    cached = _cache.get(uri)
    if cached is not None and cached.get("version") == doc.version:
        return cached["result"]

    # 重新分析
    result = _analyzer.analyze(uri, doc.text)
    _cache[uri] = {"version": doc.version, "result": result}

    # 首次分析或版本变化后，主动发布诊断
    try:
        server.publish_diagnostics(uri, list(result.diagnostics))
    except Exception:
        pass
    return result


# -- Hover -----------------------------------------------------------------

def _hover_handler(server: LspServer, uri: str, position: Position):
    analysis = _get_analysis(server, uri)
    if analysis is None:
        return None

    doc = server.documents.get(uri)
    word_info = doc.word_at_position(position)
    if word_info is None:
        return None

    word, start_pos, end_pos = word_info

    # 1) 命中文件内符号
    sym = analysis.find_definition(word, position.line)
    if sym is not None:
        # 组装 markdown 内容：kind + name + detail + doc
        parts = [f"**{sym.kind}** `{sym.name}`"]
        if sym.detail:
            parts.append("")
            parts.append(f"```lumyr\n{sym.detail}\n```")
        if sym.doc:
            parts.append("")
            parts.append(sym.doc)
        contents = MarkupContent(kind="markdown", value="\n".join(parts))
        return Hover(contents=contents,
                    range=Range(start=start_pos, end=end_pos))

    # 2) 关键字提示
    if word in KEYWORDS:
        return Hover(
            contents=MarkupContent(kind="markdown",
                                   value=f"关键字 `{word}`"),
            range=Range(start=start_pos, end=end_pos),
        )

    return None


# -- Go to Definition ------------------------------------------------------

def _definition_handler(server: LspServer, uri: str, position: Position):
    analysis = _get_analysis(server, uri)
    if analysis is None:
        return None

    doc = server.documents.get(uri)
    word_info = doc.word_at_position(position)
    if word_info is None:
        return None

    word, _, _ = word_info
    sym = analysis.find_definition(word, position.line)
    if sym is not None:
        return Location(
            uri=uri,
            range=Range(
                start=Position(line=sym.line, character=sym.column),
                end=Position(line=sym.end_line, character=sym.end_column),
            ),
        )
    return None


# -- Completion ------------------------------------------------------------

def _completion_handler(server: LspServer, uri: str, position: Position):
    analysis = _get_analysis(server, uri)
    if analysis is None:
        return CompletionList(isIncomplete=False, items=[])

    doc = server.documents.get(uri)
    word_info = doc.word_at_position(position)
    prefix = word_info[0] if word_info else ""

    items = analysis.get_completions(
        position.line, position.character, prefix
    )
    return CompletionList(isIncomplete=False, items=items)


# -- Did Save --------------------------------------------------------------

def _did_save_handler(server: LspServer, uri: str):
    """保存时强制重新分析并发布诊断。"""
    doc = server.documents.get(uri)
    if doc is None:
        return
    result = _analyzer.analyze(uri, doc.text)
    _cache[uri] = {"version": doc.version, "result": result}
    try:
        server.publish_diagnostics(uri, list(result.diagnostics))
    except Exception:
        pass


# -- 注册入口 ---------------------------------------------------------------

def register_features(server: LspServer) -> None:
    """把所有 LSP 处理器注册到给定 server 实例。"""
    server.on_hover(_hover_handler)
    server.on_definition(_definition_handler)
    server.on_completion(_completion_handler)
    server.on_did_save(_did_save_handler)


__all__ = ["register_features"]
