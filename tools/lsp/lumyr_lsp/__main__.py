"""
lumyr_lsp.__main__
===================

入口点。

使用方式：
    python -m lumyr_lsp

启动 LSP 服务器主循环。如果存在 ``lumyr_lsp.features`` 模块，
会在启动前自动导入它，从而触发功能 handler 的注册。
"""

from .server import LspServer


def main():
    """创建服务器实例并启动主循环。"""
    server = LspServer()

    # 尝试导入 features 模块（如果存在），让其注册功能 handler。
    # features 模块在被导入时会调用 server.on_hover() 等方法注册 handler。
    try:
        from . import features  # noqa: F401
    except ImportError:
        # features 模块不存在，核心服务器仍可运行
        pass

    server.run()


if __name__ == "__main__":
    main()
