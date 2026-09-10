"""LSP 端到端集成测试：直接调用 _dispatch 模拟完整 JSON-RPC 交互"""
import sys
import os
import json
import io

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

from lumyr_lsp.server import LspServer


class StdoutCapture:
    """捕获 stdout.buffer 的输出并解析 JSON-RPC 消息"""
    def __init__(self):
        self.buf = io.BytesIO()

    def get_messages(self):
        self.buf.seek(0)
        data = self.buf.read()
        messages = []
        pos = 0
        while pos < len(data):
            header_end = data.find(b"\r\n\r\n", pos)
            if header_end == -1:
                break
            header = data[pos:header_end].decode("ascii")
            cl = None
            for line in header.split("\r\n"):
                if line.lower().startswith("content-length:"):
                    cl = int(line.split(":")[1].strip())
            if cl is None:
                break
            body_start = header_end + 4
            body = data[body_start:body_start + cl]
            messages.append(json.loads(body.decode("utf-8")))
            pos = body_start + cl
        return messages

    def reset(self):
        self.buf = io.BytesIO()


def run():
    old_stdout = sys.stdout
    old_stderr = sys.stderr
    def log(*args, **kwargs):
        print(*args, file=old_stdout, **kwargs)

    log("=" * 60)
    log("LSP 端到端集成测试")
    log("=" * 60)

    server = LspServer()
    # 注册 LSP 功能（hover/definition/completion/diagnostics）
    from lumyr_lsp.features import register_features
    register_features(server)
    cap = StdoutCapture()
    def reset_capture():
        cap.buf = io.BytesIO()
        sys.stdout = io.TextIOWrapper(cap.buf, encoding="utf-8")

    cap.reset = reset_capture
    cap.reset()
    sys.stderr = io.StringIO()

    def dispatch(method, params=None, req_id=None):
        msg = {"jsonrpc": "2.0", "method": method}
        if req_id is not None:
            msg["id"] = req_id
        if params is not None:
            msg["params"] = params
        server._dispatch(msg)

    def get_responses(req_id):
        msgs = cap.get_messages()
        return [m for m in msgs if m.get("id") == req_id]

    def get_notifications(method):
        msgs = cap.get_messages()
        return [m for m in msgs if m.get("method") == method]

    try:
        # 1. initialize
        log("\n[1] initialize ...")
        dispatch("initialize", {
            "processId": None,
            "rootUri": "file:///e:/test",
            "capabilities": {}
        }, req_id=1)
        resps = get_responses(1)
        assert len(resps) >= 1, "initialize 无响应"
        caps = resps[0]["result"]["capabilities"]
        assert caps.get("hoverProvider") == True
        assert caps.get("definitionProvider") == True
        assert "completionProvider" in caps
        log(f"    hover={caps['hoverProvider']}, definition={caps['definitionProvider']}, "
              f"completion={'triggerCharacters' in caps.get('completionProvider', {})}")
        log("    OK")
        cap.reset()

        # 2. initialized
        log("\n[2] initialized ...")
        dispatch("initialized", {})
        log("    OK (通知无响应)")

        # 3. didOpen
        log("\n[3] textDocument/didOpen ...")
        test_code = """// 测试文件
func add(a, b) {
    return a + b;
}

const PI = 3.14;

func main() {
    x = add(1, 2);
    log(x);
    log(PI);
}
"""
        dispatch("textDocument/didOpen", {
            "textDocument": {
                "uri": "file:///e:/test/test.lm",
                "languageId": "lumyr",
                "version": 1,
                "text": test_code
            }
        })
        notifs = get_notifications("textDocument/publishDiagnostics")
        log(f"    诊断通知数: {len(notifs)}")
        if notifs:
            diags = notifs[0]["params"]["diagnostics"]
            log(f"    诊断条目数: {len(diags)}")
            for d in diags:
                log(f"      L{d['range']['start']['line']}: [{d.get('severity')}] {d['message']}")
        log("    OK")
        cap.reset()

        # 4. hover — 悬停在 add 调用上 (line=8: `    x = add(1, 2);`)
        log("\n[4] textDocument/hover (add 调用) ...")
        dispatch("textDocument/hover", {
            "textDocument": {"uri": "file:///e:/test/test.lm"},
            "position": {"line": 8, "character": 9}
        }, req_id=2)
        resps = get_responses(2)
        assert len(resps) >= 1
        result = resps[0].get("result")
        assert result is not None, "hover 应返回结果（add 函数）"
        contents = result.get("contents", {})
        val = contents.get("value", "") if isinstance(contents, dict) else str(contents)
        log(f"    hover: {val[:120]}")
        assert "add" in val or "function" in val.lower(), f"hover 应包含 add 函数信息: {val}"
        log("    OK")
        cap.reset()

        # 5. definition — 跳转到 add 定义 (line=8: add 调用)
        log("\n[5] textDocument/definition (add) ...")
        dispatch("textDocument/definition", {
            "textDocument": {"uri": "file:///e:/test/test.lm"},
            "position": {"line": 8, "character": 9}
        }, req_id=3)
        resps = get_responses(3)
        assert len(resps) >= 1
        result = resps[0].get("result")
        assert result is not None, "definition 应返回结果（add 函数定义）"
        loc = result if isinstance(result, dict) else result[0]
        line = loc["range"]["start"]["line"]
        col = loc["range"]["start"]["character"]
        log(f"    定义位置: L{line}:{col}")
        assert line == 1, f"add 应在 L1，实际 L{line}"
        log("    OK (正确跳转到 func add 定义)")
        cap.reset()

        # 6. completion (line=8, char=2: 空白处，前缀为空，应返回全量补全)
        log("\n[6] textDocument/completion ...")
        dispatch("textDocument/completion", {
            "textDocument": {"uri": "file:///e:/test/test.lm"},
            "position": {"line": 8, "character": 2}
        }, req_id=4)
        resps = get_responses(4)
        assert len(resps) >= 1
        result = resps[0].get("result")
        assert result is not None, "completion 应返回结果"
        items = result.get("items", []) if isinstance(result, dict) else result
        assert len(items) > 0, "补全项不应为空"
        log(f"    补全项数: {len(items)}")
        labels = [it.get("label", "") for it in items[:15]]
        log(f"    前15项: {labels}")
        all_labels = [it.get("label", "") for it in items]
        has_keyword = any(l in all_labels for l in ["func", "if", "print", "return", "for"])
        has_user = any(l in all_labels for l in ["add", "main", "PI", "x"])
        log(f"    含关键字/内置: {has_keyword}, 含用户定义符号: {has_user}")
        assert has_keyword, "补全应包含关键字"
        log("    OK")
        cap.reset()

        # 7. didChange — 增量修改 (line=8: `    x = add(1, 2);`, 把 x 改成 y)
        log("\n[7] textDocument/didChange (增量修改) ...")
        dispatch("textDocument/didChange", {
            "textDocument": {"uri": "file:///e:/test/test.lm", "version": 2},
            "contentChanges": [{
                "range": {
                    "start": {"line": 8, "character": 4},
                    "end": {"line": 8, "character": 5}
                },
                "text": "y"
            }]
        })
        doc = server.documents.get("file:///e:/test/test.lm")
        assert doc is not None
        assert doc.version == 2
        lines = doc.text.split("\n")
        log(f"    修改后第9行(0-based=8): {lines[8].strip()}")
        assert "y = add" in lines[8], f"增量修改失败: '{lines[8]}'"
        log("    OK")
        cap.reset()

        # 8. didSave
        log("\n[8] textDocument/didSave ...")
        dispatch("textDocument/didSave", {
            "textDocument": {"uri": "file:///e:/test/test.lm"}
        })
        notifs = get_notifications("textDocument/publishDiagnostics")
        log(f"    保存后诊断通知数: {len(notifs)}")
        log("    OK")
        cap.reset()

        # 9. hover on PI (常量) (line=10: `    log(PI);`, PI 在 char 10-12)
        log("\n[9] hover on PI (常量) ...")
        dispatch("textDocument/hover", {
            "textDocument": {"uri": "file:///e:/test/test.lm"},
            "position": {"line": 10, "character": 11}
        }, req_id=5)
        resps = get_responses(5)
        result = resps[0].get("result") if resps else None
        if result:
            contents = result.get("contents", {})
            val = contents.get("value", "") if isinstance(contents, dict) else str(contents)
            log(f"    hover: {val[:120]}")
        else:
            log("    hover: null")
        log("    OK")
        cap.reset()

        # 10. shutdown
        log("\n[10] shutdown ...")
        dispatch("shutdown", {}, req_id=6)
        resps = get_responses(6)
        assert len(resps) >= 1
        assert resps[0].get("result") is None
        log("    OK")

        log("\n" + "=" * 60)
        log("全部 10 项测试通过!")
        log("=" * 60)

    finally:
        sys.stdout = old_stdout
        sys.stderr = old_stderr


if __name__ == "__main__":
    run()
