# 修改 bytecode.h - 添加 BUILTIN_SEND
with open('src/ir/bytecode.h', 'r', encoding='utf-8') as f:
    content = f.read()

old = '''    BUILTIN_NEXT,            // next(gen)：恢复生成器执行，返回 yield 值；结束返回 null
    BUILTIN_COUNT
} BuiltinId;'''
new = '''    BUILTIN_NEXT,            // next(gen)：恢复生成器执行，返回 yield 值；结束返回 null
    BUILTIN_SEND,            // send(gen, val)：向生成器发送值，返回下一个 yield 值
    BUILTIN_COUNT
} BuiltinId;'''
content = content.replace(old, new)

with open('src/ir/bytecode.h', 'w', encoding='utf-8') as f:
    f.write(content)
print('bytecode.h: added BUILTIN_SEND')

# 修改 ir_compile.c - 添加 "send" 到内置函数名列表
with open('src/ir/ir_compile.c', 'r', encoding='utf-8') as f:
    content = f.read()

old = '"gc_count", "gc_bytes", "gc_collect", "gc_stw_ns", "next"};'
new = '"gc_count", "gc_bytes", "gc_collect", "gc_stw_ns", "next", "send"};'
content = content.replace(old, new)

with open('src/ir/ir_compile.c', 'w', encoding='utf-8') as f:
    f.write(content)
print('ir_compile.c: added "send" to builtin names')

# 修改 ast_typecheck.c - 添加 "send" 到内置函数列表
with open('src/ast/ast_typecheck.c', 'r', encoding='utf-8') as f:
    content = f.read()

old = '{"gc_count", 0, 0}, {"gc_bytes", 0, 0}, {"gc_collect", 0, 0}, {"gc_stw_ns", 0, 0},'
new = '{"gc_count", 0, 0}, {"gc_bytes", 0, 0}, {"gc_collect", 0, 0}, {"gc_stw_ns", 0, 0}, {"next", 1, 1}, {"send", 2, 2},'
content = content.replace(old, new)

with open('src/ast/ast_typecheck.c', 'w', encoding='utf-8') as f:
    f.write(content)
print('ast_typecheck.c: added "send" to builtins')
